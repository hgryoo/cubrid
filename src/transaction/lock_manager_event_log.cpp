/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * lock_manager_event_log.cpp - Event-log diagnostic dump helpers for the
 *                              lock manager.
 *
 * Owns the {{lock_event_log_*}} dump helpers and the LK_ENTRY xasl_id
 * accessors, isolating the event_log.h / xasl.h surface from
 * lock_manager.c.
 */

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager_internal.hpp"

#include "critical_section.h"
#include "event_log.h"
#include "heap_file.h"
#include "log_impl.h"
#include "object_representation_sr.h"
#include "oid.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "xasl.h"

#include <stdio.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * lock_event_log_tran_locks - dump transaction locks to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   tran_index(in):
 *
 *   note: for deadlock
 */
void
lock_event_log_tran_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index)
{
  int rv, i, indent = 2;
  LK_TRAN_LOCK *tran_lock;
  LK_ENTRY *entry;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  fprintf (log_fp, "hold:\n");

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  entry = tran_lock->inst_hold_list;
  for (i = 0; entry != NULL && i < MAX_NUM_LOCKS_DUMP_TO_EVENT_LOG; entry = entry->tran_next, i++)
    {
      assert (tran_index == entry->tran_index);

      fprintf (log_fp, "%*clock: %*s", indent, ' ', LOCK_MODE_STR_MAX_LENGTH,
	       lock_to_lockmode_string (entry->granted_mode));

      SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);
      lock_event_log_lock_info (thread_p, log_fp, entry);

      event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
      event_log_bind_values (thread_p, log_fp, tran_index, entry->bind_index_in_tran);

      fprintf (log_fp, "\n");

      CLEAR_EMULATE_THREAD (thread_p);
    }

  if (entry != NULL)
    {
      fprintf (log_fp, "%*c...\n", indent, ' ');
    }

  entry = tran_lock->waiting;
  if (entry != NULL)
    {
      fprintf (log_fp, "wait:\n");
      fprintf (log_fp, "%*clock: %*s", indent, ' ', LOCK_MODE_STR_MAX_LENGTH,
	       lock_to_lockmode_string (entry->blocked_mode));

      SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

      lock_event_log_lock_info (thread_p, log_fp, entry);

      event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
      event_log_bind_values (thread_p, log_fp, tran_index, entry->bind_index_in_tran);

      fprintf (log_fp, "\n");
    }
  CLEAR_EMULATE_THREAD (thread_p);

  pthread_mutex_unlock (&tran_lock->hold_mutex);
}

/*
 * lock_event_log_deadlock_locks - dump locks that caused deadlocks to event log file
 *   return: void
 *   thread_p(in): thread
 *   log_fp(in): file pointer (log)
 *   tran_index(in): transaction index selected as victim
 *   log_trunc(in): is the entries truncated
 *   log_num_entries(in): number of the entries
 *   log_entries(in): entries
 *
 *   note: for deadlock
 */
void
lock_event_log_deadlock_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, bool log_trunc,
			       int log_num_entries, LK_ENTRY ** log_entries)
{
  const char *prog, *user, *host;
  int pid;
  int rv, i, indent = 2;
  LK_TRAN_LOCK *tran_lock;
  const char *lock_name;
  LK_ENTRY *entry;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);
  assert (log_num_entries && !(log_num_entries % 2));
  assert (log_entries != NULL);

  for (i = 0; i < log_num_entries; i++)
    {
      entry = log_entries[i];

      /* holder and waiter are printed alternately */
      fprintf (log_fp, i % 2 ? "\nwait:\n" : "hold:\n");

      tran_lock = &lk_Gl.tran_lock_table[entry->tran_index];
      rv = pthread_mutex_lock (&tran_lock->hold_mutex);

      logtb_find_client_name_host_pid (entry->tran_index, &prog, &user, &host, &pid);
      fprintf (log_fp, "%*cclient: %s@%s|%s(%d)", indent, ' ', user, host, prog, pid);
      if (entry->tran_index == tran_index)
	{
	  fprintf (log_fp, " (Deadlock Victim)");
	}
      fprintf (log_fp, "\n");

      lock_name =
	(i % 2) ? lock_to_lockmode_string (entry->blocked_mode) : lock_to_lockmode_string (entry->granted_mode);
      fprintf (log_fp, "%*clock: %s", indent, ' ', lock_name);
      if (!(i % 2) && entry->blocked_mode != NULL_LOCK)
	{
	  lock_name = lock_to_lockmode_string (entry->blocked_mode);
	  fprintf (log_fp, "|waiting for lock conversion to %s", lock_name);
	}
      SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);
      lock_event_log_lock_info (thread_p, log_fp, entry);

      event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
      event_log_bind_values (thread_p, log_fp, entry->tran_index, entry->bind_index_in_tran);

      CLEAR_EMULATE_THREAD (thread_p);

      pthread_mutex_unlock (&tran_lock->hold_mutex);

      fprintf (log_fp, i % 2 ? "\n" : "");
    }

  if (log_trunc)
    {
      fprintf (log_fp, "%*c...\n\n", indent, ' ');
    }
}

/*
 * lock_event_log_blocked_lock - dump lock waiter info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   entry(in):
 *
 *   note: for lock timeout
 */
void
lock_event_log_blocked_lock (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry)
{
  int indent = 2;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

  fprintf (log_fp, "waiter:\n");
  event_log_print_client_info (entry->tran_index, indent);

  fprintf (log_fp, "%*clock: %*s", indent, ' ', LOCK_MODE_STR_MAX_LENGTH,
	   lock_to_lockmode_string (entry->blocked_mode));
  lock_event_log_lock_info (thread_p, log_fp, entry);

  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
  event_log_bind_values (thread_p, log_fp, entry->tran_index, entry->bind_index_in_tran);

  CLEAR_EMULATE_THREAD (thread_p);

  fprintf (log_fp, "\n");
}

/*
 * lock_event_log_blocking_locks - dump lock blocker info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   wait_entry(in):
 *
 *   note: for lock timeout
 */
void
lock_event_log_blocking_locks (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * wait_entry)
{
  LK_ENTRY *entry;
  LK_RES *res_ptr = NULL;
  LOCK_COMPATIBILITY compat1, compat2;
  int rv, indent = 2;
  bool is_other_waiter = false;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  res_ptr = wait_entry->res_head;
  rv = pthread_mutex_lock (&res_ptr->res_mutex);

  fprintf (log_fp, "blocker:\n");

  for (entry = res_ptr->holder; entry != NULL; entry = entry->next)
    {
      if (entry == wait_entry)
	{
	  continue;
	}

      compat1 = lock_compat (entry->granted_mode, wait_entry->blocked_mode);
      compat2 = lock_compat (entry->blocked_mode, wait_entry->blocked_mode);
      if (compat1 == LOCK_COMPAT_NO || compat2 == LOCK_COMPAT_NO)
	{
	  event_log_print_client_info (entry->tran_index, indent);

	  fprintf (log_fp, "%*clock: %*s", indent, ' ', LOCK_MODE_STR_MAX_LENGTH,
		   lock_to_lockmode_string (entry->granted_mode));

	  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

	  lock_event_log_lock_info (thread_p, log_fp, entry);

	  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
	  event_log_bind_values (thread_p, log_fp, entry->tran_index, entry->bind_index_in_tran);

	  CLEAR_EMULATE_THREAD (thread_p);

	  fprintf (log_fp, "\n");
	}
    }

  for (entry = res_ptr->waiter; entry != NULL; entry = entry->next)
    {
      if (entry == wait_entry)
	{
	  is_other_waiter = true;
	  continue;
	}

      compat1 = lock_compat (entry->blocked_mode, wait_entry->blocked_mode);
      if (compat1 == LOCK_COMPAT_NO)
	{
	  if (is_other_waiter)
	    {
	      /* first time for other waiter */
	      fprintf (log_fp, "other waiters:\n");
	      is_other_waiter = false;
	    }

	  event_log_print_client_info (entry->tran_index, indent);

	  fprintf (log_fp, "%*clock: %*s", indent, ' ', LOCK_MODE_STR_MAX_LENGTH,
		   lock_to_lockmode_string (entry->blocked_mode));

	  SET_EMULATE_THREAD_WITH_LOCK_ENTRY (thread_p, entry);

	  lock_event_log_lock_info (thread_p, log_fp, entry);

	  event_log_sql_string (thread_p, log_fp, &entry->xasl_id, indent);
	  event_log_bind_values (thread_p, log_fp, entry->tran_index, entry->bind_index_in_tran);

	  CLEAR_EMULATE_THREAD (thread_p);

	  fprintf (log_fp, "\n");
	}
    }

  pthread_mutex_unlock (&res_ptr->res_mutex);
}

/*
 * lock_event_log_lock_info - dump lock resource info to event log file
 *   return:
 *   thread_p(in):
 *   log_fp(in):
 *   entry(in):
 */
void
lock_event_log_lock_info (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry)
{
  LK_RES *res_ptr;
  char *classname;
  OID *oid_rr;

  assert (csect_check_own (thread_p, CSECT_EVENT_LOG_FILE) == 1);

  res_ptr = entry->res_head;

  fprintf (log_fp, " (oid=%d|%d|%d", res_ptr->key.oid.volid, res_ptr->key.oid.pageid, res_ptr->key.oid.slotid);

  switch (res_ptr->key.type)
    {
    case LOCK_RESOURCE_ROOT_CLASS:
      fprintf (log_fp, ", table=db_root");
      break;

    case LOCK_RESOURCE_CLASS:
      oid_rr = oid_get_rep_read_tran_oid ();
      if (oid_rr != NULL && OID_EQ (&res_ptr->key.oid, oid_rr))
	{
	  /* This is the generic object for RR transactions */
	  fprintf (log_fp, ", Generic object for Repeatable Read consistency");
	}
      else if (!OID_ISTEMP (&res_ptr->key.oid))
	{
	  OID real_class_oid;

	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &res_ptr->key.oid);
	    }

	  /* never propagate an error to get class name and keep the existing error if any. */
	  er_stack_push ();
	  (void) heap_get_class_name (thread_p, &real_class_oid, &classname);
	  er_stack_pop ();

	  if (classname != NULL)
	    {
	      fprintf (log_fp, ", table=%s", classname);
	      free_and_init (classname);
	    }
	}
      break;

    case LOCK_RESOURCE_INSTANCE:
      if (!OID_ISTEMP (&res_ptr->key.class_oid))
	{
	  OID real_class_oid;

	  if (OID_IS_VIRTUAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid))
	    {
	      OID_GET_REAL_CLASS_OF_DIR_OID (&res_ptr->key.class_oid, &real_class_oid);
	    }
	  else
	    {
	      COPY_OID (&real_class_oid, &res_ptr->key.class_oid);
	    }

	  /* never propagate an error to get class name and keep the existing error if any. */
	  er_stack_push ();
	  (void) heap_get_class_name (thread_p, &real_class_oid, &classname);
	  er_stack_pop ();

	  if (classname != NULL)
	    {
	      fprintf (log_fp, ", table=%s", classname);
	      free_and_init (classname);
	    }
	}
      break;

    default:
      break;
    }

  fprintf (log_fp, ")\n");
}

/*
 * lock_event_set_tran_wait_entry - save the lock entry tran is waiting
 *   return:
 *   entry(in):
 */
void
lock_event_set_tran_wait_entry (int tran_index, LK_ENTRY * entry)
{
  LK_TRAN_LOCK *tran_lock;
  int rv;

  tran_lock = &lk_Gl.tran_lock_table[tran_index];
  rv = pthread_mutex_lock (&tran_lock->hold_mutex);

  tran_lock->waiting = entry;

  if (entry != NULL)
    {
      lock_event_set_xasl_id_to_entry (tran_index, entry);
    }

  pthread_mutex_unlock (&tran_lock->hold_mutex);
}

/*
 * lock_event_set_xasl_id_to_entry - save the xasl id related lock entry
 *   return:
 *   entry(in):
 */
void
lock_event_set_xasl_id_to_entry (int tran_index, LK_ENTRY * entry)
{
  LOG_TDES *tdes;

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && !XASL_ID_IS_NULL (&tdes->xasl_id))
    {
      if (tdes->num_exec_queries <= MAX_NUM_EXEC_QUERY_HISTORY)
	{
	  entry->bind_index_in_tran = tdes->num_exec_queries - 1;
	}
      else
	{
	  entry->bind_index_in_tran = -1;
	}

      XASL_ID_COPY (&entry->xasl_id, &tdes->xasl_id);
    }
  else
    {
      XASL_ID_SET_NULL (&entry->xasl_id);
      entry->bind_index_in_tran = -1;
    }
}

#endif /* SERVER_MODE */
