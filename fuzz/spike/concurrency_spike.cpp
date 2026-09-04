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
#include "disk_manager.h"
#include "server_interface.h"
#include "xserver_interface.h"
#include "tz_support.h"

namespace
{
  std::atomic<int> g_created { 0 };
  std::atomic<int> g_failed { 0 };

  double
  now_sec ()
  {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
  }

  const char *
  isvalid_str (DISK_ISVALID v)
  {
    return (v == DISK_VALID) ? "VALID" : (v == DISK_INVALID) ? "INVALID" : "ERROR";
  }

  /* The oracle.  Sanitizers see a race only when it manifests as a bad access;
   * silent page or tracker corruption is the class this workload is most likely
   * to produce, and nothing but the engine's own checks will see it.
   *
   * Run before *and* after: a check that only runs afterwards cannot tell a
   * defect this workload caused from one the database already had. */
  bool
  run_checks (THREAD_ENTRY * thread_p, const char *when, bool full)
  {
    bool ok = true;
    double t0;

    /* The checks scan classes and take locks.  Running them on the caller's
     * system transaction index would leak those locks until shutdown, where
     * lock_uninit_resource () asserts res_ptr->holder == NULL -- which is
     * exactly what happened the first time.  dblink_2pc_daemon.c:359 documents
     * the same trap and the same fix: give the work its own transaction so the
     * locks are released through the normal commit path. */
    int tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE, NULL, NULL,
					      TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
    if (tran_index == NULL_TRAN_INDEX)
      {
	printf ("  %-6s could not assign a transaction index for the checks\n", when);
	return false;
      }

    t0 = now_sec ();
    DISK_ISVALID d = disk_check (thread_p, false /* no repair */);
    printf ("  %-6s disk_check            %-8s %6.3f s\n", when, isvalid_str (d), now_sec () - t0);
    ok &= (d == DISK_VALID);

    t0 = now_sec ();
    DISK_ISVALID f = file_tracker_check (thread_p);
    printf ("  %-6s file_tracker_check    %-8s %6.3f s\n", when, isvalid_str (f), now_sec () - t0);
    ok &= (f == DISK_VALID);

    if (full)
      {
	t0 = now_sec ();
	int rc = xboot_check_db_consistency (thread_p, CHECKDB_ALL_CHECK_EXCEPT_PREV_LINK, NULL, 0, NULL);
	printf ("  %-6s check_db_consistency  rc=%-5d  %6.3f s\n", when, rc, now_sec () - t0);
	ok &= (rc == NO_ERROR);
      }

    xtran_server_commit (thread_p, false);
    logtb_free_tran_index (thread_p, tran_index);

    return ok;
  }

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

	/* xheap_create (), not file_create_heap ().  The latter has exactly one
	 * real caller -- heap_file.c:4793, inside heap creation -- and calling it
	 * alone produces a file with no sticky first page, which is a heap only
	 * halfway.  CHECKDB_HEAP_CHECK_ALLHEAPS then trips
	 * file_get_sticky_first_page ()'s assert_release (false).  The wider
	 * oracle found that on its first run, in this harness rather than in the
	 * engine, which is what a stricter oracle does first. */
	OID class_oid;
	OID_SET_NULL (&class_oid);
	HFID hfid;
	HFID_SET_NULL (&hfid);

	int rc = xheap_create (te, &hfid, &class_oid, false);
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

  setvbuf (stdout, NULL, _IONBF, 0);   /* an abort must not swallow the log */

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

  const bool full = (getenv ("FUZZ_FULL_CHECK") != NULL);

  printf ("oracle, before:\n");
  bool clean_before = run_checks (thread_p, "before", full);
  if (!clean_before)
    {
      printf ("  database is not clean before the workload -- anything found after\n"
	      "  this point cannot be attributed to it.  Stopping.\n");
      xboot_shutdown_server (thread_p, ER_ALL_FINAL);
      cubthread::finalize ();
      return 2;
    }

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

  printf ("oracle, after:\n");
  bool clean_after = run_checks (thread_p, "after", full);
  if (!clean_after)
    {
      printf ("\nFINDING: the database was clean before this workload and is not now.\n");
    }

  bool ok = xboot_shutdown_server (thread_p, ER_ALL_FINAL);
  cubthread::finalize ();
  printf ("shutdown ok=%d\n", (int) ok);
  return (g_failed.load () == 0 && ok && clean_after) ? 0 : 1;
}
