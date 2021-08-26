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

#ifndef _METHOD_QUERY_STRUCT_HPP_
#define _METHOD_QUERY_STRUCT_HPP_

#ident "$Id$"

#include <string>
#include <vector>

#include "dbtype_def.h"
#include "packable_object.hpp"

namespace cubmethod
{
  // forward declaration
  struct query_result;

  struct column_info : public cubpacking::packable_object
  {
    column_info ();
    column_info (short scale, int prec, char charset,
		 std::string col_name, std::string default_value, char auto_increment,
		 char unique_key, char primary_key, char reverse_index, char reverse_unique,
		 char foreign_key, char shared, std::string attr_name, std::string class_name,
		 char nullable);

    std::string class_name;
    std::string attr_name;

    short scale;
    int prec;
    char charset;
    std::string col_name;
    char is_non_null;

    char auto_increment;
    char unique_key;
    char primary_key;
    char reverse_index;
    char reverse_unique;
    char foreign_key;
    char shared;
    std::string default_value_string;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct prepare_info : public cubpacking::packable_object
  {
    int handle_id;
    char stmt_type;
    int num_markers;
    std::vector<column_info> column_infos; // num_columns = column_infos.size()

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct query_result_info
  {

  };

  struct execute_info
  {
    int num_affected;
    int num_q_result;
    std::vector<query_result_info> query_result_infos;

    char stmt_type;
    int num_markers;

    bool include_column_info;
    std::vector<column_info> column_infos;
  };

  int make_prepare_column_list_info (query_result &qresult);
}

#endif