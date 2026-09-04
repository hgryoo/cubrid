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
#include "connection_sr.h"
#include "session.h"
#include "heap_file.h"
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
  std::atomic<int> g_created { 0 };     /* heaps created */
  std::atomic<int> g_dropped { 0 };     /* heaps dropped again */
  std::atomic<int> g_inserted { 0 };    /* records inserted */
  std::atomic<int> g_overflow { 0 };    /* of those, ones too big for a page */
  std::atomic<int> g_aborted { 0 };     /* transactions rolled back on purpose */
  std::atomic<int> g_failed { 0 };
  std::atomic<bool> g_said_insert_error { false };

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

  /* Record sizes, chosen so the set straddles the page.  The last one is past
   * heap_is_big_length (), so heap_insert_logical () promotes it to an overflow
   * file instead of a home page -- a different set of latches, and the reason
   * for having more than one size at all. */
  const int RECORD_SIZES[] = { 64, 512, 4096, 20000 };
  const int RECORDS_PER_HEAP = 8;

  /* Fill a heap.  Nothing here is derived from fuzzer input: Tier 1 varies the
   * *interleaving*, not the arguments (§7.2 -- the OS saturates the ordering
   * space on its own), so records are well-formed by construction and a finding
   * is about concurrency rather than about a malformed argument.
   *
   * The shape is the one boot_sr.c:5057 uses for the same kind of heap -- a
   * heap with no user class behind it: the root class OID, a NULL scancache,
   * REC_HOME, and a leading int of zero for the representation id and flag
   * bits.  That int is zero for the reason tde.c:202 spells out: vacuum reads
   * it while undoing, so it cannot be left as whatever the allocator returned.
   *
   * The class OID is the *root* class rather than NULL, and that is not
   * cosmetic.  tde.c:210 does pass NULL, but heap_insert_logical () takes an
   * unconditional IX lock on context->class_oid (heap_file.c:23389) and
   * lock_internal_perform_lock_object () asserts a lock target is not the null
   * OID -- tde gets away with it only because it runs at createdb time.  The
   * root class is lockable and MVCC treats it exactly as it treats NULL
   * (mvcc_is_mvcc_disabled_class (): 'OID_ISNULL || OID_IS_ROOTOID'), so the
   * record path is unchanged and the lock is a real one.
   *
   * This was the second time §5's hazard bit this harness in one session, and
   * both times the oracle rather than a reviewer is what caught it. */
  int
  fill_heap (THREAD_ENTRY * te, HFID * hfid, int seed)
  {
    int inserted = 0;

    for (int k = 0; k < RECORDS_PER_HEAP; k++)
      {
	const int size = RECORD_SIZES[(seed + k) % (int) (sizeof (RECORD_SIZES) / sizeof (RECORD_SIZES[0]))];
	char *buf = (char *) malloc (size);
	if (buf == NULL)
	  {
	    g_failed++;
	    break;
	  }
	memset (buf, (char) ('a' + (k % 26)), size);
	int repid_and_flag_bits = 0;
	memcpy (buf, &repid_and_flag_bits, sizeof (int));

	RECDES recdes;
	recdes.length = recdes.area_size = size;
	recdes.type = REC_HOME;
	recdes.data = buf;

	HEAP_OPERATION_CONTEXT ctx;
	heap_create_insert_context (&ctx, hfid, oid_Root_class_oid, &recdes, NULL);
	if (heap_insert_logical (te, &ctx, NULL) == NO_ERROR)
	  {
	    inserted++;
	    if (size > 16 * 1024)
	      {
		g_overflow++;
	      }
	  }
	else
	  {
	    g_failed++;
	    /* Say why, once.  A silent failure count is how 320 failed inserts
	     * looked like a working workload for one whole run. */
	    bool expected = false;
	    if (g_said_insert_error.compare_exchange_strong (expected, true))
	      {
		fprintf (stderr, "insert failed (size %d): %s\n", size, er_msg ()? er_msg () : "no message");
	      }
	  }
	free (buf);
      }

    return inserted;
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
     * A harness thread has to reproduce that setup rather than invent one.
     *
     * TT_WORKER is not a label, it is a promise.  log_tran_table.c:2896 states
     * it -- "Only TT_WORKER threads use pl_session" -- and cubpl::get_session ()
     * (pl_session.cpp:50) returns quietly for every other type while going to
     * thread_p->conn_entry->session_p for this one.  log_tdes::lock_topop ()
     * reaches for the PL session unconditionally, so a TT_WORKER with no
     * connection entry makes every log_sysop_start () set
     * ER_SES_SESSION_EXPIRED as a side effect; heap_insert_logical () returns
     * it, and every insert fails for a reason unrelated to what it was asked
     * to do.
     *
     * The fix is to keep the promise rather than to pick a type that dodges it.
     * Borrowing TT_RECOVERY would compile and run -- nothing branches on it
     * today -- but it names startup parallel redo (log_recovery.c,
     * log_recovery_redo_parallel.cpp), and this harness runs against a booted
     * server doing ordinary work.  A type that is wrong but currently harmless
     * is a defect waiting for the first branch on it.
     *
     * So the thread gets a real connection entry and a real session.
     * css_init_conn_list () runs inside boot_restart_server (boot_sr.c:2164),
     * so the CSS_CONN_ENTRY array exists here even though no listener does, and
     * css_initialize_conn () only stores the fd -- it never touches the socket
     * -- so INVALID_SOCKET yields a genuine connection entry with no
     * connection behind it.  session_state_create () then builds the session,
     * its PL session and its private LRU, and attaches all of it through
     * session_set_conn_entry_data (). */
    te->register_id ();
    te->type = TT_WORKER;
    te->m_status = cubthread::entry::status::TS_RUN;
    te->shutdown = false;
    te->m_px_orig_thread_entry = NULL;
    te->m_uses_px_stats = false;
    te->m_px_stats = NULL;
    te->get_error_context ().register_thread_local ();

    /* register_id () first, and not as a matter of taste: the connection list
     * is guarded by an rmutex, and rmutex_lock () asserts the caller's thread id
     * is registered (critical_section.c:2175).  Taking a connection before
     * registering trips that assert. */
    CSS_CONN_ENTRY *conn = css_make_conn (INVALID_SOCKET);
    if (conn == NULL)
      {
	fprintf (stderr, "[t%d] no connection entry\n", id);
	g_failed++;
	te->unregister_id ();
	cubthread::get_manager ()->retire_entry (*te);
	return;
      }
    /* css_make_conn () takes the entry off the free list; the server puts it on
     * the active list once the connection is accepted
     * (master_connector.cpp:1141).  Skipping that is not cosmetic either:
     * session_state_verify_ref_count () counts references by walking the active
     * list and asserts outright when it is empty (session.c:3086). */
    css_insert_into_active_conn_list (conn);
    te->conn_entry = conn;

    SESSION_ID session_id = DB_EMPTY_SESSION;
    if (session_state_create (te, &session_id) != NO_ERROR)
      {
	fprintf (stderr, "[t%d] no session: %s\n", id, er_msg ()? er_msg () : "");
	g_failed++;
      }

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
	/* A real request puts its transaction index on the connection, which is
	 * how css_find_conn_by_tran_index () and the interrupt path find it. */
	conn->set_tran_index (tran_index);

	/* One iteration is a heap's whole life: create it, fill it, drop it.
	 * Dropping is what makes a database reusable between runs -- an earlier
	 * version of this spike created heaps and never dropped them, so the
	 * *before* half of the oracle failed on the leftovers and every run
	 * needed a database of its own.
	 *
	 * xheap_create (), not file_create_heap ().  The latter has exactly one
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
	if (rc != NO_ERROR)
	  {
	    g_failed++;
	    (void) xtran_server_abort (te);
	    logtb_free_tran_index (te, tran_index);
	    continue;
	  }

	g_created++;
	g_inserted += fill_heap (te, &hfid, id + i);

	/* Every fourth iteration rolls back instead, so undo runs against a heap
	 * another thread is concurrently creating and filling.  Abort also
	 * undoes the create, so there is nothing left to drop on that path. */
	if (i % 4 == 3)
	  {
	    (void) xtran_server_abort (te);
	    g_aborted++;
	  }
	else
	  {
	    xtran_server_commit (te, false);

	    /* The drop is its own transaction, the way DROP TABLE is its own
	     * statement.  Committing ends the transaction but keeps the index,
	     * so the next call starts a new one on it -- which is how a client
	     * connection issues one statement after another. */
	    if (xheap_destroy (te, &hfid, &class_oid) == NO_ERROR)
	      {
		g_dropped++;
		xtran_server_commit (te, false);
	      }
	    else
	      {
		g_failed++;
		(void) xtran_server_abort (te);
	      }
	  }

	conn->set_tran_index (NULL_TRAN_INDEX);
	logtb_free_tran_index (te, tran_index);
      }

    if (session_id != DB_EMPTY_SESSION)
      {
	(void) session_state_destroy (te, session_id, false);
      }
    te->get_error_context ().deregister_thread_local ();
    te->end_resource_tracks ();
    te->conn_entry = NULL;
    css_prepare_shutdown_conn (conn);
    css_free_conn (conn);
    /* unregister_id () last, for the same reason register_id () was first:
     * css_free_conn () takes the connection-list rmutex. */
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
  printf ("heaps dropped  %d\n", g_dropped.load ());
  printf ("records        %d  (%d overflow)\n", g_inserted.load (), g_overflow.load ());
  printf ("aborted trans  %d\n", g_aborted.load ());
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
