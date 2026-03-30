#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cas_cci.h"

static void
die_usage (void)
{
  fprintf (stderr,
	   "usage:\n"
	   "  cci_ann_runner build-index <conn_url> <db_user> <db_pass> <m> <ef_construction>\n"
	   "  cci_ann_runner query-ann <conn_url> <db_user> <db_pass> <ef_search> <topk> <query_id_file>\n"
	   "\n"
	   "optional env:\n"
	   "  CCI_QUERY_PROFILE_FILE=<path>  write per-query timing CSV for query-ann\n");
  exit (1);
}

static long long
monotonic_ns (void)
{
  struct timespec ts;

  if (clock_gettime (CLOCK_MONOTONIC, &ts) != 0)
    {
      fprintf (stderr, "clock_gettime failed: %s\n", strerror (errno));
      exit (1);
    }

  return (long long) ts.tv_sec * 1000000000LL + (long long) ts.tv_nsec;
}

static double
ns_to_sec (long long ns)
{
  return (double) ns / 1000000000.0;
}

static void
die_cci (const char *stage, int err_code, T_CCI_ERROR * err_buf)
{
  const char *msg = "";

  if (err_buf != NULL && err_buf->err_msg[0] != '\0')
    {
      msg = err_buf->err_msg;
    }

  fprintf (stderr, "%s failed: code=%d msg=%s\n", stage, err_code, msg);
  exit (2);
}

static int
connect_db (const char *conn_url, const char *db_user, const char *db_pass, T_CCI_ERROR * err_buf)
{
  int conn = cci_connect_with_url_ex ((char *) conn_url, (char *) db_user, (char *) db_pass, err_buf);
  if (conn < 0)
    {
      die_cci ("cci_connect_with_url_ex", conn, err_buf);
    }

  if (cci_set_autocommit (conn, CCI_AUTOCOMMIT_FALSE) < 0)
    {
      die_cci ("cci_set_autocommit", -1, err_buf);
    }

  return conn;
}

static void
disconnect_db (int conn, T_CCI_ERROR * err_buf)
{
  if (conn >= 0)
    {
      int rc = cci_disconnect (conn, err_buf);
      if (rc < 0)
	{
	  die_cci ("cci_disconnect", rc, err_buf);
	}
    }
}

static int
prepare_stmt (int conn, const char *sql, T_CCI_ERROR * err_buf)
{
  int stmt = cci_prepare (conn, sql, 0, err_buf);
  if (stmt < 0)
    {
      die_cci ("cci_prepare", stmt, err_buf);
    }
  return stmt;
}

static void
close_stmt (int stmt)
{
  if (stmt >= 0)
    {
      int rc = cci_close_req_handle (stmt);
      if (rc < 0)
	{
	  fprintf (stderr, "warning: cci_close_req_handle failed: %d\n", rc);
	}
    }
}

static void
exec_stmt_no_result (int conn, const char *sql, T_CCI_ERROR * err_buf)
{
  int stmt = -1;
  int rc;

  stmt = prepare_stmt (conn, sql, err_buf);
  rc = cci_execute (stmt, 0, 0, err_buf);
  if (rc < 0)
    {
      close_stmt (stmt);
      die_cci ("cci_execute", rc, err_buf);
    }

  rc = cci_end_tran (conn, CCI_TRAN_COMMIT, err_buf);
  if (rc < 0)
    {
      close_stmt (stmt);
      die_cci ("cci_end_tran", rc, err_buf);
    }

  close_stmt (stmt);
}

static void
run_build_index (const char *conn_url, const char *db_user, const char *db_pass, int m, int ef_construction)
{
  T_CCI_ERROR err_buf;
  int conn = -1;
  char sql[512];

  memset (&err_buf, 0, sizeof (err_buf));
  conn = connect_db (conn_url, db_user, db_pass, &err_buf);

  snprintf (sql, sizeof (sql),
	    "CREATE VECTOR INDEX vidx_nytimes_train "
	    "ON nytimes_256_angular_train (vec COSINE) "
	    "WITH (M = %d, ef_construction = %d)",
	    m, ef_construction);
  exec_stmt_no_result (conn, sql, &err_buf);
  disconnect_db (conn, &err_buf);
}

static void
fetch_ann_rows (int stmt, int qid, T_CCI_ERROR * err_buf, int *row_count, long long *first_row_ns, long long *fetch_done_ns)
{
  int rc;
  int rid = 0;
  int indicator = 0;
  T_CCI_CURSOR_POS cursor_pos = CCI_CURSOR_FIRST;
  int local_row_count = 0;
  long long local_first_row_ns = 0;

  while (1)
    {
      rc = cci_cursor (stmt, 1, cursor_pos, err_buf);
      if (rc == CCI_ER_NO_MORE_DATA)
	{
	  break;
	}
      if (rc < 0)
	{
	  die_cci ("cci_cursor", rc, err_buf);
	}

      cursor_pos = CCI_CURSOR_CURRENT;
      rc = cci_fetch (stmt, err_buf);
      if (rc < 0)
	{
	  die_cci ("cci_fetch", rc, err_buf);
	}

      rc = cci_get_data (stmt, 1, CCI_A_TYPE_INT, &rid, &indicator);
      if (rc < 0)
	{
	  die_cci ("cci_get_data", rc, err_buf);
	}

      if (indicator >= 0)
	{
	  if (local_row_count == 0)
	    {
	      local_first_row_ns = monotonic_ns ();
	    }
	  local_row_count++;
	  printf ("1|%d|%d\n", qid, rid);
	}
    }

  if (row_count != NULL)
    {
      *row_count = local_row_count;
    }
  if (first_row_ns != NULL)
    {
      *first_row_ns = local_first_row_ns;
    }
  if (fetch_done_ns != NULL)
    {
      *fetch_done_ns = monotonic_ns ();
    }
}

static void
fetch_query_vector (int stmt, int qid, T_CCI_ERROR * err_buf, char *vector_buf, size_t vector_buf_size)
{
  int rc;
  int indicator = 0;
  char *vector_str = NULL;

  rc = cci_bind_param (stmt, 1, CCI_A_TYPE_INT, &qid, CCI_U_TYPE_INT, 0);
  if (rc < 0)
    {
      die_cci ("cci_bind_param(vector)", rc, err_buf);
    }

  rc = cci_execute (stmt, 0, 0, err_buf);
  if (rc < 0)
    {
      die_cci ("cci_execute(vector)", rc, err_buf);
    }

  rc = cci_cursor (stmt, 1, CCI_CURSOR_FIRST, err_buf);
  if (rc < 0)
    {
      die_cci ("cci_cursor(vector)", rc, err_buf);
    }

  rc = cci_fetch (stmt, err_buf);
  if (rc < 0)
    {
      die_cci ("cci_fetch(vector)", rc, err_buf);
    }

  rc = cci_get_data (stmt, 1, CCI_A_TYPE_STR, &vector_str, &indicator);
  if (rc < 0)
    {
      die_cci ("cci_get_data(vector)", rc, err_buf);
    }

  if (indicator < 0 || vector_str == NULL)
    {
      fprintf (stderr, "vector lookup returned NULL for query id %d\n", qid);
      exit (1);
    }

  snprintf (vector_buf, vector_buf_size, "%s", vector_str);

  rc = cci_close_query_result (stmt, err_buf);
  if (rc < 0)
    {
      die_cci ("cci_close_query_result(vector)", rc, err_buf);
    }
}

static void
run_query_ann (const char *conn_url, const char *db_user, const char *db_pass, int ef_search, int topk,
	       const char *query_id_file)
{
  T_CCI_ERROR err_buf;
  int conn = -1;
  int stmt = -1;
  int vector_stmt = -1;
  FILE *fp = NULL;
  FILE *profile_fp = NULL;
  char set_sql[128];
  char vector_sql[256];
  char query_sql[1024];
  char line[128];
  char vector_buf[32768];
  const char *profile_path = getenv ("CCI_QUERY_PROFILE_FILE");

  memset (&err_buf, 0, sizeof (err_buf));
  conn = connect_db (conn_url, db_user, db_pass, &err_buf);

  snprintf (set_sql, sizeof (set_sql), "SET SYSTEM PARAMETERS 'hnsw_ef_search=%d'", ef_search);
  exec_stmt_no_result (conn, set_sql, &err_buf);

  snprintf (vector_sql, sizeof (vector_sql),
	    "/* profile:vector_lookup */ "
	    "SELECT CAST(vec AS STRING) "
	    "FROM nytimes_256_angular_test "
	    "WHERE id = ?");
  vector_stmt = prepare_stmt (conn, vector_sql, &err_buf);

  snprintf (query_sql, sizeof (query_sql),
	    "SELECT /* profile:ann_query */ /*+ recompile no_parallel_heap_scan */ id "
	    "FROM nytimes_256_angular_train "
	    "ORDER BY vec <c> CAST(? AS vector) "
	    "LIMIT %d",
	    topk);
  stmt = prepare_stmt (conn, query_sql, &err_buf);
  cci_fetch_size (stmt, topk);

  fp = fopen (query_id_file, "r");
  if (fp == NULL)
    {
      fprintf (stderr, "failed to open query id file %s: %s\n", query_id_file, strerror (errno));
      close_stmt (stmt);
      disconnect_db (conn, &err_buf);
      exit (1);
    }

  if (profile_path != NULL && profile_path[0] != '\0')
    {
      profile_fp = fopen (profile_path, "w");
      if (profile_fp == NULL)
	{
	  fprintf (stderr, "failed to open query profile file %s: %s\n", profile_path, strerror (errno));
	  fclose (fp);
	  close_stmt (stmt);
	  disconnect_db (conn, &err_buf);
	  exit (1);
	}
      fprintf (profile_fp,
	       "query_id,vector_lookup_sec,bind_sec,execute_sec,first_row_wait_sec,fetch_sec,close_result_sec,total_sec,row_count\n");
    }

  while (fgets (line, sizeof (line), fp) != NULL)
    {
      int qid = 0;
      int rc;
      int row_count = 0;
      long long bind_start_ns;
      long long bind_end_ns;
      long long vector_lookup_start_ns;
      long long vector_lookup_end_ns;
      long long execute_start_ns;
      long long execute_end_ns;
      long long first_row_ns = 0;
      long long fetch_done_ns = 0;
      long long close_start_ns;
      long long close_end_ns;

      if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
	{
	  continue;
	}

      qid = (int) strtol (line, NULL, 10);
      vector_lookup_start_ns = monotonic_ns ();
      fetch_query_vector (vector_stmt, qid, &err_buf, vector_buf, sizeof (vector_buf));
      vector_lookup_end_ns = monotonic_ns ();
      bind_start_ns = monotonic_ns ();
      rc = cci_bind_param (stmt, 1, CCI_A_TYPE_STR, vector_buf, CCI_U_TYPE_STRING, 0);
      bind_end_ns = monotonic_ns ();
      if (rc < 0)
	{
	  if (profile_fp != NULL)
	    {
	      fclose (profile_fp);
	    }
	  fclose (fp);
	  close_stmt (stmt);
	  disconnect_db (conn, &err_buf);
	  die_cci ("cci_bind_param", rc, &err_buf);
	}

      execute_start_ns = monotonic_ns ();
      rc = cci_execute (stmt, 0, 0, &err_buf);
      execute_end_ns = monotonic_ns ();
      if (rc < 0)
	{
	  if (profile_fp != NULL)
	    {
	      fclose (profile_fp);
	    }
	  fclose (fp);
	  close_stmt (stmt);
	  disconnect_db (conn, &err_buf);
	  die_cci ("cci_execute", rc, &err_buf);
	}

      fetch_ann_rows (stmt, qid, &err_buf, &row_count, &first_row_ns, &fetch_done_ns);

      close_start_ns = monotonic_ns ();
      rc = cci_close_query_result (stmt, &err_buf);
      close_end_ns = monotonic_ns ();
      if (rc < 0)
	{
	  if (profile_fp != NULL)
	    {
	      fclose (profile_fp);
	    }
	  fclose (fp);
	  close_stmt (stmt);
	  disconnect_db (conn, &err_buf);
	  die_cci ("cci_close_query_result", rc, &err_buf);
	}

      if (profile_fp != NULL)
	{
	  double bind_sec = ns_to_sec (bind_end_ns - bind_start_ns);
	  double vector_lookup_sec = ns_to_sec (vector_lookup_end_ns - vector_lookup_start_ns);
	  double execute_sec = ns_to_sec (execute_end_ns - execute_start_ns);
	  double first_row_wait_sec = first_row_ns > 0 ? ns_to_sec (first_row_ns - execute_end_ns) : 0.0;
	  double fetch_sec = fetch_done_ns > 0 ? ns_to_sec (fetch_done_ns - execute_end_ns) : 0.0;
	  double close_result_sec = ns_to_sec (close_end_ns - close_start_ns);
	  double total_sec = ns_to_sec (close_end_ns - vector_lookup_start_ns);

	  fprintf (profile_fp, "%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d\n",
		   qid, vector_lookup_sec, bind_sec, execute_sec, first_row_wait_sec, fetch_sec, close_result_sec,
		   total_sec, row_count);
	}
    }

  if (profile_fp != NULL)
    {
      fclose (profile_fp);
    }
  fclose (fp);
  close_stmt (vector_stmt);
  close_stmt (stmt);
  disconnect_db (conn, &err_buf);
}

int
main (int argc, char *argv[])
{
  if (argc < 2)
    {
      die_usage ();
    }

  if (strcmp (argv[1], "build-index") == 0)
    {
      if (argc != 7)
	{
	  die_usage ();
	}

      run_build_index (argv[2], argv[3], argv[4], atoi (argv[5]), atoi (argv[6]));
      return 0;
    }

  if (strcmp (argv[1], "query-ann") == 0)
    {
      if (argc != 8)
	{
	  die_usage ();
	}

      run_query_ann (argv[2], argv[3], argv[4], atoi (argv[5]), atoi (argv[6]), argv[7]);
      return 0;
    }

  die_usage ();
  return 1;
}
