/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
 * jsp_sr.h - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#ifndef _JSP_SR_H_
#define _JSP_SR_H_

#include "dbtype_def.h"
#include "porting.h"
#include "thread_compat.hpp"

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */

#ident "$Id$"

extern int jsp_start_server (const char *server_name, const char *path);
extern int jsp_stop_server (void);
extern int jsp_server_port (void);
extern int jsp_jvm_is_loaded (void);

extern char *jsp_pack_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_int_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_bigint_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_short_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_float_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_double_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_numeric_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_string_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_date_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_time_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_timestamp_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_datetime_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_set_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_object_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_monetary_argument (char *buffer, DB_VALUE * value);
extern char *jsp_pack_null_argument (char *buffer);

extern char *jsp_unpack_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_int_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_bigint_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_short_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_float_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_double_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_numeric_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_string_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_date_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_time_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_timestamp_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_set_value (char *buffer, int type, DB_VALUE * retval);
extern char *jsp_unpack_object_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_monetary_value (char *buffer, DB_VALUE * retval);
extern char *jsp_unpack_resultset (char *buffer, DB_VALUE * retval);

extern SOCKET jsp_connect_server (void);
extern int jsp_get_value_size (DB_VALUE * value);
extern int jsp_get_argument_size (DB_VALUE ** argarray, const int arg_cnt);
extern int jsp_send_call_request (THREAD_ENTRY * thread_p, SOCKET sock_fd, DB_VALUE ** argarray, const char *name, const int arg_cnt);
extern int jsp_send_call_main (THREAD_ENTRY * thread_p, SOCKET *sock_fd, DB_VALUE ** argarray, const char *name, const int arg_cnt);

extern int jsp_writen (SOCKET fd, const void *vptr, int n);
extern int jsp_readn (SOCKET fd, void *vptr, int n);

extern int jsp_alloc_response (THREAD_ENTRY * thread_p, SOCKET sock_fd, char *&buffer);
extern int jsp_receive_response_main (THREAD_ENTRY * thread_p, SOCKET sock_fd, DB_VALUE *result);
extern void jsp_close_connection_socket (SOCKET *sock_fd);

#endif /* _JSP_SR_H_ */
