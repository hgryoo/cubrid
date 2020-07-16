/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * cas_procedure.h -
 */

#ifndef	_CAS_PROCEDURE_H_
#define	_CAS_PROCEDURE_H_

#include "cas_network.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct p_buf P_BUF;
struct p_buf
{
  int err_code;
  int alloc_size;
  int data_size;
  char *data;
};

extern int fr_get_cursor_count (int srv_h_id);

extern int fr_get_cursor_pos (int srv_h_id);

extern int fr_move_cursor (int srv_h_id, int offset, int origin);

extern int fr_fetch (int srv_h_id, int cursor_pos, int fetch_count, char fetch_flag);

extern int fr_peek_cursor (int srv_h_id, char fetch_flag, T_NET_BUF * net_buf);

#ifdef __cplusplus
}
#endif

#endif /* _CAS_PROCEDURE_H_ */
