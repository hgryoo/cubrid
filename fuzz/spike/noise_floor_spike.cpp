/*
 * noise_floor_spike.cpp - how much interleaving does the OS produce on its own?
 *
 * This measures the prerequisite for schedule fuzzing, which the design records
 * as its most dangerous open question (N66 10-design_fi-rendezvous.md §10 Q2).
 * Coverage feedback over an encoded schedule pays only if mutating the schedule
 * produces a different *and reachable* interleaving.  Between wait points the OS
 * still schedules, so if that noise alone already produces every ordering, a
 * schedule mutation is indistinguishable from noise and the search degenerates
 * to random -- at tens of executions per second, which is worse than random with
 * a fast one.
 *
 * So: hold the input fixed and repeat it.  Each participant takes a ticket from
 * a shared counter immediately before its operation; the ticket order is the
 * observed interleaving.  Count distinct orders over N repetitions.
 *
 *   few distinct orders  -> the OS is close to deterministic here, and a
 *                           schedule mutation has room to show up
 *   near T! distinct     -> the noise floor is the whole space; nothing a
 *                           schedule says will be visible above it
 *
 * The --noop mode is the control: it takes the ticket and does no engine work,
 * so it separates plain thread-scheduling noise from ordering the engine itself
 * imposes through its own latches.
 *
 * Threads are persistent and synchronise on a barrier per repetition, so what is
 * measured is the operation's interleaving rather than thread-startup jitter.
 *
 * Not a fuzz target and not in the build.  Build against a build tree:
 *   clang++-18 -std=c++17 -DSERVER_MODE $(includes) -o noise_floor_spike \
 *       noise_floor_spike.cpp -L<build>/cubrid -lcubrid -latomic
 *   ./noise_floor_spike <dbname> [threads] [reps] [--noop]
 */

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "boot_sr.h"
#include "critical_section.h"
#include "error_manager.h"
#include "file_manager.h"
#include "log_impl.h"
#include "memory_monitor_sr.hpp"
#include "message_catalog.h"
#include "oid.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include "tz_support.h"
#include "xserver_interface.h"

namespace
{
  int g_threads = 4;
  int g_reps = 200;
  bool g_noop = false;

  std::mutex g_m;
  std::condition_variable g_cv;
  int g_round = 0;              /* controller bumps this to start a round */
  int g_done = 0;               /* participants bump this as they finish */

  std::atomic<int> g_ticket { 0 };
  std::vector<int> g_order;     /* g_order[ticket] = participant id */

  std::atomic<int> g_failed { 0 };

  void
  participant (int id)
  {
    cubthread::entry *te = cubthread::get_manager ()->claim_entry ();
    if (te == NULL)
      {
	g_failed++;
	return;
      }
    /* entry_manager::create_context (thread_entry_task.cpp) -- reproduced,
     * not invented; the engine asserts on the pieces. */
    te->register_id ();
    te->type = TT_WORKER;
    te->m_status = cubthread::entry::status::TS_RUN;
    te->shutdown = false;
    te->m_px_orig_thread_entry = NULL;
    te->m_uses_px_stats = false;
    te->m_px_stats = NULL;
    te->get_error_context ().register_thread_local ();

    int seen = 0;
    while (true)
      {
	{
	  std::unique_lock<std::mutex> lk (g_m);
	  g_cv.wait (lk, [&] { return g_round != seen; });
	  seen = g_round;
	  if (seen < 0)
	    {
	      break;
	    }
	}

	/* The observation point.  Everything before it is barrier overhead;
	 * the ticket order is what the run actually did. */
	int t = g_ticket.fetch_add (1, std::memory_order_relaxed);
	g_order[t] = id;

	if (!g_noop)
	  {
	    int tran = logtb_assign_tran_index (te, NULL_TRANID, TRAN_ACTIVE, NULL, NULL,
						TRAN_LOCK_INFINITE_WAIT, TRAN_READ_COMMITTED);
	    if (tran != NULL_TRAN_INDEX)
	      {
		OID class_oid;
		OID_SET_NULL (&class_oid);
		VFID vfid = VFID_INITIALIZER;
		if (file_create_heap (te, false, &class_oid, &vfid) == NO_ERROR)
		  {
		    xtran_server_commit (te, false);
		  }
		else
		  {
		    g_failed++;
		    (void) xtran_server_abort (te);
		  }
		logtb_free_tran_index (te, tran);
	      }
	    else
	      {
		g_failed++;
	      }
	  }

	{
	  std::lock_guard<std::mutex> lk (g_m);
	  g_done++;
	}
	g_cv.notify_all ();
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
      fprintf (stderr, "usage: %s <dbname> [threads] [reps] [--noop]\n", argv[0]);
      return 2;
    }
  const char *db_name = argv[1];
  if (argc > 2)
    g_threads = atoi (argv[2]);
  if (argc > 3)
    g_reps = atoi (argv[3]);
  for (int i = 1; i < argc; i++)
    if (strcmp (argv[i], "--noop") == 0)
      g_noop = true;

  THREAD_ENTRY *thread_p = NULL;

#define STEP(expr, what)                                                      \
  do { int rc_ = (expr);                                                      \
       if (rc_ != NO_ERROR) { fprintf (stderr, "stopped at %s rc=%d\n", (what), rc_); return 1; } \
  } while (0)

  STEP (er_init (NULL, ER_NEVER_EXIT), "er_init");
  cubthread::initialize (thread_p);
  STEP (msgcat_init (), "msgcat_init");
  STEP (tz_load (), "tz_load");
  STEP (sysprm_load_and_init (db_name, NULL, SYSPRM_LOAD_ALL), "sysprm");
  sysprm_set_er_log_file (db_name);
  STEP (sync_initialize_sync_stats (), "sync_stats");
  STEP (csect_initialize_static_critical_sections (), "csect");
  STEP (er_init (NULL, prm_get_integer_value (PRM_ID_ER_EXIT_ASK)), "er_init2");
#if !defined(WINDOWS)
  STEP (mmon_initialize (db_name), "mmon");
#endif
  CHECK_ARGS check = { true, true };
  STEP (boot_restart_server (thread_p, false, db_name, false, &check, NULL, false), "boot");

  g_order.resize (g_threads);

  std::vector<std::thread> ts;
  for (int i = 0; i < g_threads; i++)
    ts.emplace_back (participant, i);

  std::map<std::string, int> orders;
  for (int rep = 0; rep < g_reps; rep++)
    {
      g_ticket.store (0);
      std::fill (g_order.begin (), g_order.end (), -1);
      {
	std::lock_guard<std::mutex> lk (g_m);
	g_done = 0;
	g_round++;
      }
      g_cv.notify_all ();
      {
	std::unique_lock<std::mutex> lk (g_m);
	g_cv.wait (lk, [&] { return g_done == g_threads; });
      }

      std::string key;
      for (int i = 0; i < g_threads; i++)
	key += char ('0' + (g_order[i] < 0 ? 9 : g_order[i]));
      orders[key]++;
    }

  {
    std::lock_guard<std::mutex> lk (g_m);
    g_round = -1;
  }
  g_cv.notify_all ();
  for (auto &t : ts)
    t.join ();

  long fact = 1;
  for (int i = 2; i <= g_threads; i++)
    fact *= i;

  printf ("mode              %s\n", g_noop ? "noop (control)" : "file_create_heap");
  printf ("threads           %d   reps %d   failures %d\n", g_threads, g_reps, g_failed.load ());
  printf ("distinct orders   %zu of %ld possible\n", orders.size (), fact);
  printf ("coverage of space %.1f%%\n", 100.0 * (double) orders.size () / (double) fact);

  std::vector<std::pair<int, std::string>> top;
  for (auto &kv : orders)
    top.emplace_back (kv.second, kv.first);
  std::sort (top.rbegin (), top.rend ());
  printf ("most frequent:\n");
  for (size_t i = 0; i < top.size () && i < 5; i++)
    printf ("  %-10s %5d  (%.1f%%)\n", top[i].second.c_str (), top[i].first,
	    100.0 * top[i].first / g_reps);

  xboot_shutdown_server (thread_p, ER_ALL_FINAL);
  cubthread::finalize ();
  return 0;
}
