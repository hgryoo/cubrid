/*
 *
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
// method_query_cursor.hpp: implement callback for server-side driver's request
//

#ifndef _METHOD_CALLBACK_HPP_
#define _METHOD_CALLBACK_HPP_

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "method_query.hpp"
#include "method_query_struct.hpp"
#include "packer.hpp"

namespace cubmethod
{
  class log_handler
  {

  };

  class error_context
  {
    public:
      int get_error ();
      int set_error (int err_number, int err_indicator, std::string file, int line);
      int set_error_with_msg (int err_number, int err_indicator, std::string err_msg, std::string err_file, int line,
			      bool force);

    private:
      int err_indicator;
      int err_number;
      std::string err_string;
      std::string err_file;
      int err_line;
  };



  class callback_handler
  {
    public:
      callback_handler (int max_query_handler);

      cubmem::block callback_dispatch (packing_unpacker &unpacker);

      /* query handler required */
      cubmem::block prepare (packing_unpacker &unpacker);
      cubmem::block execute (packing_unpacker &unpacker);

      int oid_get (OID oid);
      int oid_put (OID oid);
      int oid_cmd (char cmd, OID oid);

      int collection_cmd (char cmd, OID oid, int seq_index, std::string attr_name);
      int col_get (DB_COLLECTION *col, char col_type, char ele_type, DB_DOMAIN *ele_domain);
      int col_size (DB_COLLECTION *col);
      int col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val);

      int lob_new (DB_TYPE lob_type);
      int lob_write (DB_VALUE *lob_dbval, int64_t offset, int size,  char *data);
      int lob_read (DB_TYPE lob_type);

    private:
      /* ported from cas_handle */
      int new_query_handler ();
      query_handler *find_query_handler (int id);
      void free_query_handle (int id);
      void free_query_handle_all ();


      error_context m_error_ctx;
      std::vector<query_handler *> m_query_handlers;
  };
}

#endif