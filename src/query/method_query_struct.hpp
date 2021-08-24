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

namespace cubmethod
{
    // forward declaration
    struct query_result;

    struct column_info
    {
        DB_OBJECT *class_obj;
        DB_ATTRIBUTE *attr;

        char ut;
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
    };

    struct prepare_result
    {
        int64_t handle_id;
        char stmt_id;
        int num_markers;
        int num_columns;
        std::vector<column_info> column_infos;
    };

    int make_prepare_column_list_info (query_result& qresult);
}

#endif