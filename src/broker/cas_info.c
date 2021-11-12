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
 * cas_info.c -
 */

#ident "$Id$"

#include "cas_info.h"

void
pack_user_info (T_NET_BUF * net_buf, T_USER_INFO * user_info)
{
  net_buf_cp_int (net_buf, user_info->broker_name.size () + 1, NULL);
  net_buf_cp_str (net_buf, user_info->broker_name.c_str (), user_info->broker_name.size () + 1);	// broker name

  net_buf_cp_int (net_buf, user_info->cas_name.size () + 1, NULL);
  net_buf_cp_str (net_buf, user_info->cas_name.c_str (), user_info->cas_name.size () + 1);	// cas name

  net_buf_cp_int (net_buf, user_info->db_name.size () + 1, NULL);
  net_buf_cp_str (net_buf, user_info->db_name.c_str (), user_info->db_name.size () + 1);	// database name

  net_buf_cp_int (net_buf, user_info->db_user.size () + 1, NULL);
  net_buf_cp_str (net_buf, user_info->db_user.c_str (), user_info->db_user.size () + 1);	// database host

  net_buf_cp_int (net_buf, user_info->client_ip.size () + 1, NULL);
  net_buf_cp_str (net_buf, user_info->client_ip.c_str (), user_info->client_ip.size () + 1);	// database user
}
