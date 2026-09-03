/*
 * concurrency_spike.cpp - Tier 1: concurrent storage work in one process,
 *                         with the one window-widening hook the engine has.
 *
 * cubrid-testkit §6a-E9 §5.4 splits the storage target in two.  Tier 2 wants a
 * rendezvous handler so an interleaving can be *replayed*; the engine has no
 * such primitive yet.  Tier 1 is what is possible today: run the real thing
 * concurrently under sanitizers, widen the one window that has a hold hook, and
 * see whether anything falls over.  Races surface probabilistically rather than
 * on demand, which is the whole difference between the tiers.
 *
 * The path exercised is the one CBRD-27198 was written for.  file_create_heap ()
 * reaches disk_reserve_sectors () (file_manager.c:3402), and
 * disk_reserve_sectors_in_volume () carries
 * FI_TEST_DISK_MANAGER_VOLHEADER_HOLD (disk_manager.c:4096) -- a handler that
 * sleeps while holding the volume header, so several threads reserving in the
 * same volume actually collide instead of missing each other by microseconds.
 *
 * FI is armed per thread: state lives in thread_p->fi_test_array, so only the
 * designated thread holds and the others run into it.  That is the part of a
 * schedule the engine can express today.
 *
 * Requires a Debug build -- FI_TEST compiles to (NO_ERROR) under NDEBUG, so a
 * release build would run the same code with no hook at all.
 *
 *   clang++-18 -std=c++17 -fsanitize=address,undefined -DSERVER_MODE \
 *       $(includes from build_fuzz) -o concurrency_spike concurrency_spike.cpp \
 *       -L<build_fuzz>/cubrid -lcubrid -latomic
 *   ./concurrency_spike <dbname> [threads] [iterations] [hold_seconds]
 */

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "config.h"
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "fault_injection.h"
#include "file_manager.h"
#include "log_impl.h"
#include "memory_monitor_sr.hpp"
#include "message_catalog.h"
#include "oid.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "xserver_interface.h"
#include "tz_support.h"

namespace
{
  std::atomic<int> g_created { 0 };
  std::atomic<int> g_failed { 0 };

  void
  worker (int id, int iterations, int hold_seconds)
  {
    cubthread::entry *te = cubthread::get_manager ()->claim_entry ();
    if (te == NULL)
      {
	fprintf (stderr, "[t%d] no thread entry\n", id);
	g_failed++;
	return;
      }

    /* Claiming an entry is not enough.  The worker pool sets up a context around
     * it (entry_manager::create_context, thread_entry_task.cpp) and the engine
     * asserts on the pieces: rmutex_lock () wants the id registered, and
     * thread_suspend_timeout_wakeup_and_unlock_entry () wants m_status == TS_RUN.
     * A harness thread has to reproduce that setup rather than invent one. */
    te->register_id ();
    te->type = TT_WORKER;
    te->m_status = cubthread::entry::status::TS_RUN;
    te->shutdown = false;
    te->m_px_orig_thread_entry = NULL;
    te->m_uses_px_stats = false;
    te->m_px_stats = NULL;
    te->get_error_context ().register_thread_local ();

    /* Thread 0 is the one that holds the volume header open.  The others are
     * what run into it.  FI state is per entry, so this arms only this thread. */
    if (id == 0 && hold_seconds > 0)
      {
	fi_thread_init (te);
	if (fi_set (te, FI_TEST_DISK_MANAGER_VOLHEADER_HOLD, hold_seconds) != NO_ERROR)
	  {
	    fprintf (stderr, "[t%d] fi_set failed -- is this a Debug build?\n", id);
	  }
      }
    else
      {
	fi_thread_init (te);
      }

    for (int i = 0; i < iterations; i++)
      {
	int tran_index = logtb_assign_tran_index (te, NULL_TRANID, TRAN_ACTIVE, NULL, NULL,
						  TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
	if (tran_index == NULL_TRAN_INDEX)
	  {
	    g_failed++;
	    break;
	  }

	OID class_oid;
	OID_SET_NULL (&class_oid);
	VFID vfid = VFID_INITIALIZER;

	int rc = file_create_heap (te, false, &class_oid, &vfid);
	if (rc == NO_ERROR)
	  {
	    g_created++;
	    xtran_server_commit (te, false);
	  }
	else
	  {
	    g_failed++;
	    (void) xtran_server_abort (te);
	  }

	logtb_free_tran_index (te, tran_index);
      }

    te->get_error_context ().deregister_thread_local ();
    te->end_resource_tracks ();
    te->unregister_id ();
    cubthread::get_manager ()->retire_entry (*te);
  }
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <dbname> [threads] [iterations] [hold_seconds]\n", argv[0]);
      return 2;
    }
  const char *db_name = argv[1];
  const int nthreads = (argc > 2) ? atoi (argv[2]) : 4;
  const int iters = (argc > 3) ? atoi (argv[3]) : 20;
  const int hold = (argc > 4) ? atoi (argv[4]) : 1;

  THREAD_ENTRY *thread_p = NULL;

#define STEP(expr, what)                                                      \
  do {                                                                        \
    int rc_ = (expr);                                                         \
    if (rc_ != NO_ERROR)                                                      \
      {                                                                       \
        fprintf (stderr, "stopped at %s: rc=%d %s\n", (what), rc_,            \
                 er_msg () ? er_msg () : "");                                 \
        return 1;                                                             \
      }                                                                       \
  } while (0)

  STEP (er_init (NULL, ER_NEVER_EXIT), "er_init");
  cubthread::initialize (thread_p);
  STEP (msgcat_init (), "msgcat_init");
  STEP (tz_load (), "tz_load");
  STEP (sysprm_load_and_init (db_name, NULL, SYSPRM_LOAD_ALL), "sysprm_load_and_init");
  sysprm_set_er_log_file (db_name);
  STEP (sync_initialize_sync_stats (), "sync_initialize_sync_stats");
  STEP (csect_initialize_static_critical_sections (), "csect_initialize");
  STEP (er_init (NULL, prm_get_integer_value (PRM_ID_ER_EXIT_ASK)), "er_init (params)");
#if !defined(WINDOWS)
  STEP (mmon_initialize (db_name), "mmon_initialize");
#endif

  CHECK_ARGS check = { true, true };
  STEP (boot_restart_server (thread_p, false, db_name, false, &check, NULL, false), "boot_restart_server");

  printf ("booted.  threads=%d iterations=%d hold=%ds\n", nthreads, iters, hold);

  std::vector<std::thread> ts;
  for (int i = 0; i < nthreads; i++)
    {
      ts.emplace_back (worker, i, iters, hold);
    }
  for (auto &t : ts)
    {
      t.join ();
    }

  printf ("heaps created  %d\n", g_created.load ());
  printf ("failures       %d\n", g_failed.load ());

  bool ok = xboot_shutdown_server (thread_p, ER_ALL_FINAL);
  cubthread::finalize ();
  printf ("shutdown ok=%d\n", (int) ok);
  return (g_failed.load () == 0 && ok) ? 0 : 1;
}
