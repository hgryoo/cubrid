#ifndef _RTREE_H_
#define _RTREE_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"

#include "oid.h"
#include "object_domain.h"
#include "storage_common.h"
#include "thread_compat.hpp"

extern BTID *xrtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type, OID * class_oid,
			       int attr_id);
extern BTID *xrtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *constraint_name,
				TP_DOMAIN * key_type, OID * class_oids, int n_classes, int n_attrs,
				int *attr_ids, HFID * hfids);
extern int xrtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid);

#endif /* _RTREE_H_ */
