/*
 * reset_spike.cpp - does the engine come back to a known state between inputs?
 *
 * SCOPE, corrected 2026-09-03: this was written as the entry gate for the
 * storage fuzz target and it is not one.  It runs in SA mode, and SA is
 * single-threaded, while the defects that target exists for are made by
 * interleavings -- slot reuse under concurrent update, latch acquisition order,
 * vacuum interfering with a reader.  The storage layer is mode-split code
 * besides (146 SERVER_MODE branches in page_buffer.c, 53 in log_manager.c, 26
 * in vacuum.c), so what is observed here does not carry to a SERVER_MODE
 * server, and that target links libcubrid.so rather than the SA library this
 * links.
 *
 * What it does establish is the *setup* half: whether a transaction boundary
 * returns the database to a known starting state, deterministically and
 * cheaply.  It does, and that part is still worth having -- see
 * cubrid-testkit E9/requirements.md §5.2.  Reproducibility for the real target
 * has to come from replaying a schedule, not from returning to identical
 * state, and that is unmeasured.
 *
 * So: boot once, then run the same fixed operation sequence N times, resetting
 * between runs, and check that every run observes exactly the same thing.  One
 * distinct digest means the strategy is deterministic; more than one means it
 * is not, and says so rather than being argued about.
 *
 * Not a fuzz target and deliberately not in the build: it answers a question
 * about feasibility, and adding build surface before it does would be backwards.
 *
 *   g++ -O2 -o reset_spike reset_spike.cpp \
 *       -I$CUBRID/include -L$CUBRID/lib -lcubridsa
 *   ./reset_spike <dbname> <iterations> <abort|recreate|restart>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

#include "dbi.h"

namespace
{

  const char *PROG = "reset_spike";

  double
  now_sec ()
  {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
  }

  /* FNV-1a over everything a run observes.  Any divergence between runs --
   * a different row order, a different value, a different error -- lands here. */
  struct digest
  {
    unsigned long long h = 1469598103934665603ULL;

    void feed (const char *s, size_t n)
    {
      for (size_t i = 0; i < n; i++)
	{
	  h ^= (unsigned char) s[i];
	  h *= 1099511628211ULL;
	}
    }
    void feed (const std::string & s)
    {
      feed (s.data (), s.size ());
    }
    void feed_int (long long v)
    {
      feed ((const char *) &v, sizeof (v));
    }
  };

  /* Run one statement.  The error code is part of what a run observes: a
   * sequence that fails the same way every time is still deterministic, and a
   * sequence that fails differently is exactly what this is looking for. */
  int
  run_sql (const char *sql, digest & d)
  {
    DB_QUERY_RESULT *result = NULL;
    DB_QUERY_ERROR query_error;
    int rc = db_execute (sql, &result, &query_error);

    d.feed (sql, strlen (sql));
    d.feed_int (rc < 0 ? rc : 0);

    if (rc >= 0 && result != NULL)
      {
	/* Walk the rows so the read path is observed, not only the write path. */
	int rows = 0;
	while (db_query_next_tuple (result) == DB_CURSOR_SUCCESS)
	  {
	    rows++;
	  }
	d.feed_int (rows);
	db_query_end (result);
      }
    else
      {
	d.feed_int (-1);
      }
    return rc;
  }

  /* The sequence under test (variant 0).  Deliberately small and deliberately
   * touching the paths E9 cares about: insert, in-place and growing update,
   * delete, and a scan that has to see the result of all three. */
  void
  op_sequence_0 (digest & d)
  {
    run_sql ("INSERT INTO fuzz_t VALUES (1, 'a')", d);
    run_sql ("INSERT INTO fuzz_t VALUES (2, 'bb')", d);
    run_sql ("UPDATE fuzz_t SET v = 'ccc' WHERE k = 1", d);
    run_sql ("INSERT INTO fuzz_t VALUES (3, 'ddd')", d);
    run_sql ("DELETE FROM fuzz_t WHERE k = 2", d);
    run_sql ("SELECT k, v FROM fuzz_t ORDER BY k", d);
  }

  /* Two more shapes, run *between* variant-0 runs.  Repeating one sequence only
   * shows that a run does not disturb an identical run.  A fuzzer never does
   * that: every input differs from the one before it, so what has to hold is
   * that variant 0's digest is the same no matter what ran before it.  That is
   * what these are for -- they leave different residue behind (a wider row, an
   * aborted transaction, a rolled-back unique violation) and variant 0 has to
   * be blind to all of it. */
  void
  op_sequence_1 (digest & d)
  {
    run_sql ("INSERT INTO fuzz_t VALUES (7, 'wide-value-that-does-not-fit-in-place-aaaaaaaaaaaaaaaa')", d);
    run_sql ("UPDATE fuzz_t SET v = 'x' WHERE k = 7", d);
    run_sql ("SELECT count(*) FROM fuzz_t", d);
  }

  void
  op_sequence_2 (digest & d)
  {
    run_sql ("INSERT INTO fuzz_t VALUES (9, 'dup')", d);
    run_sql ("INSERT INTO fuzz_t VALUES (9, 'dup')", d);	/* unique violation */
    db_abort_transaction ();
    run_sql ("SELECT count(*) FROM fuzz_t", d);
  }

  void
  op_sequence (int variant, digest & d)
  {
    switch (variant)
      {
      case 1:
	op_sequence_1 (d);
	break;
      case 2:
	op_sequence_2 (d);
	break;
      default:
	op_sequence_0 (d);
	break;
      }
  }

  bool
  reset_abort (const char *)
  {
    db_abort_transaction ();
    digest sink;
    run_sql ("DELETE FROM fuzz_t", sink);
    db_commit_transaction ();
    return true;
  }

  bool
  reset_recreate (const char *)
  {
    db_abort_transaction ();
    digest sink;
    run_sql ("DROP TABLE fuzz_t", sink);
    run_sql ("CREATE TABLE fuzz_t (k INT PRIMARY KEY, v VARCHAR(64))", sink);
    db_commit_transaction ();
    return true;
  }

  bool
  reset_restart (const char *dbname)
  {
    db_abort_transaction ();
    if (db_shutdown () != NO_ERROR)
      {
	return false;
      }
    if (db_login ("DBA", NULL) != NO_ERROR || db_restart (PROG, 0, dbname) != NO_ERROR)
      {
	fprintf (stderr, "restart failed: %s\n", db_error_string (3));
	return false;
      }
    digest sink;
    run_sql ("DELETE FROM fuzz_t", sink);
    db_commit_transaction ();
    return true;
  }

}				// namespace

int
main (int argc, char **argv)
{
  if (argc < 4)
    {
      fprintf (stderr, "usage: %s <dbname> <iterations> <abort|recreate|restart>\n", argv[0]);
      return 2;
    }
  const char *dbname = argv[1];
  const int iters = atoi (argv[2]);
  const std::string strategy = argv[3];

  bool (*reset) (const char *) = NULL;
  if (strategy == "abort")
    {
      reset = reset_abort;
    }
  else if (strategy == "recreate")
    {
      reset = reset_recreate;
    }
  else if (strategy == "restart")
    {
      reset = reset_restart;
    }
  else
    {
      fprintf (stderr, "unknown strategy '%s'\n", strategy.c_str ());
      return 2;
    }

  if (db_login ("DBA", NULL) != NO_ERROR || db_restart (PROG, 0, dbname) != NO_ERROR)
    {
      fprintf (stderr, "boot failed: %s\n", db_error_string (3));
      return 1;
    }
  db_set_lock_timeout (-1);

  {
    digest sink;
    run_sql ("DROP TABLE fuzz_t", sink);	/* may fail; that is fine */
    if (run_sql ("CREATE TABLE fuzz_t (k INT PRIMARY KEY, v VARCHAR(64))", sink) < 0)
      {
	fprintf (stderr, "setup failed: %s\n", db_error_string (3));
	db_shutdown ();
	return 1;
      }
    db_commit_transaction ();
  }

  std::vector < unsigned long long >digests;
  std::vector < double >times;
  digests.reserve (iters);
  times.reserve (iters);

  for (int i = 0; i < iters; i++)
    {
      double t0 = now_sec ();
      if (!reset (dbname))
	{
	  fprintf (stderr, "reset failed at iteration %d\n", i);
	  db_shutdown ();
	  return 1;
	}
      /* 0, 1, 0, 2, 0, 1, ... -- every other run is variant 0, and what
       * precedes it keeps changing. */
      const int variant = (i % 2 == 0) ? 0 : ((i / 2) % 2 == 0 ? 1 : 2);
      digest d;
      op_sequence (variant, d);
      db_commit_transaction ();
      times.push_back (now_sec () - t0);
      if (variant == 0)
	{
	  digests.push_back (d.h);
	}
    }

  db_shutdown ();

  std::vector < unsigned long long >uniq = digests;
  std::sort (uniq.begin (), uniq.end ());
  uniq.erase (std::unique (uniq.begin (), uniq.end ()), uniq.end ());

  std::vector < double >st = times;
  std::sort (st.begin (), st.end ());
  double total = 0;
  for (double t : times)
    {
      total += t;
    }

  printf ("strategy           %s\n", strategy.c_str ());
  printf ("iterations         %d  (%zu of them variant 0, interleaved with variants 1 and 2)\n",
	  iters, digests.size ());
  printf ("distinct digests   %zu   %s\n", uniq.size (), uniq.size () == 1 ? "DETERMINISTIC" : "NOT DETERMINISTIC");
  printf ("per-iteration sec  min %.6f  median %.6f  max %.6f\n", st.front (), st[st.size () / 2], st.back ());
  printf ("iterations/sec     %.1f\n", total > 0 ? iters / total : 0.0);

  if (uniq.size () != 1)
    {
      printf ("\nfirst divergence:\n");
      for (size_t i = 0; i < digests.size (); i++)
	{
	  if (digests[i] != digests[0])
	    {
	      printf ("  variant-0 run %zu digest %016llx != run 0 digest %016llx\n", i, digests[i], digests[0]);
	      break;
	    }
	}
      return 1;
    }
  return 0;
}
