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
 * cas_info.h - getter for user infos
 */

#ifndef	_CAS_INFO_H_
#define	_CAS_INFO_H_

#include <string>

#include "cas_network.h"
#include "cas_net_buf.h"

typedef struct t_user_info T_USER_INFO;
struct t_user_info
{
  std::string broker_name;
  std::string cas_name;
  std::string db_name;
  std::string db_user;
  std::string client_ip;
};

extern T_USER_INFO *get_user_info ();
extern void pack_user_info (T_NET_BUF * net_buf, T_USER_INFO * user_info);

#endif
