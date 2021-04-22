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
 * jsp_tt.hpp - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#ifndef _JSP_PACKING_HPP_
#define _JSP_PACKING_HPP_

#include "packable_object.hpp"

#include <cassert>

namespace cubprocedure
{
  struct sp_arg : public cubpacking::packable_object
  {
    sp_arg ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
    size_t get_value_packed_size (cubpacking::packer &serializator, std::size_t start_offset, DB_VALUE *val) const;

    void pack_db_value (cubpacking::packer &serializator, DB_VALUE *val) const;

    DB_VALUE *value;
    int arg_mode;
    int arg_type;
  };

  struct sp_info : public cubpacking::packable_object
  {
    sp_info ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override {}
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override { return 0; }

    std::string name;
    std::vector<sp_arg> args;
    int arg_count;
    int return_type;
  };

}

#endif