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

#ifndef _SCAN_JAVASP_HPP_
#define _SCAN_JAVASP_HPP_

#include "storage_common.h"
#include "method_scan.h"

#include "jsp_comm.h"
#include "jsp_sr.h"

#include "packer.hpp"

#include <vector>

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

struct qproc_db_value_list;
struct val_list_node;
struct qfile_list_id;
struct scan_id_struct;

namespace cubscan
{
  namespace javasp
  {
    class scanner
    {
      public:
	scanner();

	void init (cubthread::entry *thread_p, qfile_list_id *list_id, method_sig_list *meth_sig_list);
	int open ();
	SCAN_CODE next_scan (val_list_node &vl);
	int close ();

      protected:

//////////////////////////////////////////////////////////////////////////
// Communication with Java SP Server routine declarations
//////////////////////////////////////////////////////////////////////////

	bool connect ();
	bool disconnect ();
	int request (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals);
	int receive (METHOD_SIG *&method_sig, DB_VALUE *v);
	int invoke ()
	{
	  return 0;
	}

//////////////////////////////////////////////////////////////////////////
// Serialization and Deserialization of DB_VALUE
//////////////////////////////////////////////////////////////////////////

	int pack_request (METHOD_SIG *&method_sig);
	void pack_arg (cubpacking::packer &serializator, METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals, int &strlen);
	void pack_arg_value (cubpacking::packer &serializator, DB_VALUE &v);
	void unpack_result (cubpacking::unpacker &deserializator, DB_VALUE *retval);

	size_t get_request_size (METHOD_SIG *&method_sig, int &strlen, std::vector<DB_VALUE> &arg_vals);
	size_t get_arg_value_size (DB_VALUE &val);
	size_t get_arg_size (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals);

	int get_argument_count (METHOD_SIG *&method_sig)
	{
	  return method_sig->num_method_args;
	}
//////////////////////////////////////////////////////////////////////////
// Value array scanning declarations
//////////////////////////////////////////////////////////////////////////

	int open_value_array ();
	SCAN_CODE next_value_array (val_list_node &vl);
	int close_value_array ();

	int get_single_tuple_from_list_id (METHOD_SIG *sig, qfile_list_id *list_id_p, std::vector<DB_VALUE> &vec);

      private:

	SOCKET m_sock_fd = INVALID_SOCKET;
	bool is_connected = false;

	METHOD_SCAN_BUFFER m_scan_buf;	/* value array buffer */
	cubthread::entry *m_thread_p;
	METHOD_INFO *m_method_ctl;

	int m_num_args;
    };
  }
}

// naming convention of SCAN_ID's
using JAVASP_SCAN_ID = cubscan::javasp::scanner;

#endif