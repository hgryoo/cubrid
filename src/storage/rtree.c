#include "config.h"

#include "rtree.h"

#include <assert.h>

#include "error_manager.h"

BTID *
xrtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type, OID * class_oid, int attr_id)
{
  (void) thread_p;
  (void) btid;
  (void) key_type;
  (void) class_oid;
  (void) attr_id;

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NOT_SUPPORTED_OPERATION, 0);
  assert (false);
  return NULL;
}

BTID *
xrtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *constraint_name, TP_DOMAIN * key_type,
		   OID * class_oids, int n_classes, int n_attrs, int *attr_ids, HFID *hfids)
{
  (void) thread_p;
  (void) btid;
  (void) constraint_name;
  (void) key_type;
  (void) class_oids;
  (void) n_classes;
  (void) n_attrs;
  (void) attr_ids;
  (void) hfids;

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NOT_SUPPORTED_OPERATION, 0);
  assert (false);
  return NULL;
}

int
xrtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid)
{
  (void) thread_p;
  (void) btid;

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERFACE_NOT_SUPPORTED_OPERATION, 0);
  assert (false);
  return ER_INTERFACE_NOT_SUPPORTED_OPERATION;
}
