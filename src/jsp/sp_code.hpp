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

//
// method_query_code.hpp
//

#ifndef _METHOD_QUERY_CODE_HPP_
#define _METHOD_QUERY_CODE_HPP_

#ident "$Id$"

#include <vector>

#include "dbtype_def.h"
#include "query_list.h" /* QUERY_ID, QFILE_LIST_ID */
#include "query_manager.h"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

enum SP_CODE_ATTRIBUTES
{
  SPC_ATTR_NAME_INDEX,
  SPC_ATTR_CREATED_TIME,
  SPC_ATTR_OWNER_INDEX,
  SPC_ATTR_IS_STATIC_INDEX,
  SPC_ATTR_IS_SYSTEM_GENERATED_INDEX,
  SPC_ATTR_STYPE_INDEX,
  SPC_ATTR_SCODE_INDEX,
  SPC_ATTR_OTYPE_INDEX,
  SPC_ATTR_OCODE_INDEX,
  SPC_ATTR_MAX_INDEX
};

struct sp_code_entry
{
  OID oid;			/* stored procedure code object identifier */

  /* stored procedure code object values */
  DB_VALUE name;                // STRING
  DB_VALUE created_time;        // STRING
  DB_VALUE owner;               // OBJECT
  DB_VALUE is_static;           // INTEGER
  DB_VALUE is_system_generated; // INTEGER
  DB_VALUE stype;               // INTEGER
  DB_VALUE scode;               // STRING
  DB_VALUE otype;               // INTEGER
  DB_VALUE ocode;               // STRING
};

int sp_get_code_attr (THREAD_ENTRY *thread_p, const std::string &attr_name, const OID *sp_oidp, DB_VALUE *result);
#endif				/* _METHOD_QUERY_CODE_HPP_ */
