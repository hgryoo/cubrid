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

#include "method_query_struct.hpp"

#include "method_query_util.hpp"
#include "language_support.h" /* lang_charset(); */

namespace cubmethod
{
  column_info::column_info ()
  {
    // default constructor
    scale = 0;
    prec = 0;
    charset = lang_charset();
    is_non_null = 0;

    /* col_name, attr_name, class_name, default_value leave as empty */
    // FIXME: to remove warning
    col_name.clear ();
    attr_name.clear ();
    class_name.clear ();
    default_value_string.clear();

    auto_increment = 0;
    unique_key = 0;
    primary_key = 0;
    reverse_index = 0;
    reverse_unique = 0;
    foreign_key = 0;
    shared = 0;
  }

  column_info::column_info (short scale, int prec, char charset,
			    std::string col_name, std::string default_value, char auto_increment,
			    char unique_key, char primary_key, char reverse_index, char reverse_unique,
			    char foreign_key, char shared, std::string attr_name, std::string class_name,
			    char is_non_null)
  {
    this->scale = scale;
    this->prec = prec;
    this->charset = charset;

    this->col_name.assign (col_name);
    stmt_trim (this->col_name);

    this->attr_name.assign (attr_name);
    this->class_name.assign (class_name);
    this->default_value_string.assign (default_value);

    if (is_non_null >= 1)
      {
	this->is_non_null = 1;
      }
    else if (is_non_null < 0)
      {
	this->is_non_null = 0;
      }

    this->auto_increment = auto_increment;
    this->unique_key = unique_key;
    this->primary_key = primary_key;
    this->reverse_index = reverse_index;
    this->reverse_unique = reverse_unique;
    this->foreign_key = foreign_key;
    this->shared = shared;
  }

  void
  column_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (charset);
    serializator.pack_short (scale);
    serializator.pack_int (prec);
    serializator.pack_string (col_name);
    serializator.pack_string (attr_name);
    serializator.pack_string (class_name);
    serializator.pack_int (is_non_null);
    serializator.pack_string (default_value_string);

    serializator.pack_int (auto_increment);
    serializator.pack_int (unique_key);
    serializator.pack_int (primary_key);
    serializator.pack_int (reverse_index);
    serializator.pack_int (reverse_unique);
    serializator.pack_int (foreign_key);
    serializator.pack_int (shared);
  }

  void
  column_info::unpack (cubpacking::unpacker &deserializator)
  {
    int cs, p, inn;
    deserializator.unpack_int (cs);
    deserializator.unpack_short (scale);
    deserializator.unpack_int (p);

    deserializator.unpack_string (col_name);
    deserializator.unpack_string (attr_name);
    deserializator.unpack_string (class_name);
    deserializator.unpack_int (inn);
    deserializator.unpack_string (default_value_string);

    charset = (char) cs;
    prec = (char) p;
    is_non_null = (char) inn;

    int a, u, pk, ri, ru, f, s;
    deserializator.unpack_int (a);
    deserializator.unpack_int (u);
    deserializator.unpack_int (pk);
    deserializator.unpack_int (ri);
    deserializator.unpack_int (ru);
    deserializator.unpack_int (f);
    deserializator.unpack_int (s);

    auto_increment = (char) a;
    unique_key = (char) u;
    primary_key = (char) pk;
    reverse_index = (char) ri;
    reverse_unique = (char) ru;
    foreign_key = (char) f;
    shared = (char) s;
  }

  size_t
  column_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // charset
    size += serializator.get_packed_short_size (size); // scale
    size += serializator.get_packed_int_size (size); // prec
    size += serializator.get_packed_string_size (col_name, size); // col_name
    size += serializator.get_packed_string_size (attr_name, size); // attr_name
    size += serializator.get_packed_string_size (class_name, size); // class_name
    size += serializator.get_packed_int_size (size); // is_non_null
    size += serializator.get_packed_string_size (default_value_string, size); // default_value_string

    size += serializator.get_packed_int_size (size); // auto_increment
    size += serializator.get_packed_int_size (size); // unique_key
    size += serializator.get_packed_int_size (size); // primary_key
    size += serializator.get_packed_int_size (size); // reverse_index
    size += serializator.get_packed_int_size (size); // reverse_unique
    size += serializator.get_packed_int_size (size); // foreign_key
    size += serializator.get_packed_int_size (size); // shared
    return size;
  }

  void
  prepare_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (handle_id);
    serializator.pack_int (stmt_type);
    serializator.pack_int (num_markers);
    serializator.pack_int (column_infos.size());
    for (int i = 0; i < column_infos.size(); i++)
      {
	column_infos[i].pack (serializator);
      }
  }

  void
  prepare_info::unpack (cubpacking::unpacker &deserializator)
  {
    int type, num_column_info;
    deserializator.unpack_int (handle_id);
    deserializator.unpack_int (type);
    deserializator.unpack_int (num_markers);
    deserializator.unpack_int (num_column_info);

    if (num_column_info > 0)
      {
	column_info tmp_info;
	for (int i = 0; i < num_column_info; i++)
	  {
	    tmp_info.unpack (deserializator);
	  }
	column_infos.push_back (tmp_info);
      }
    stmt_type = (char) type;
  }

  size_t
  prepare_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // handle_id
    size += serializator.get_packed_int_size (size); // type
    size += serializator.get_packed_int_size (size); // num_markers
    size += serializator.get_packed_int_size (size); // num_columns

    if (column_infos.size() > 0)
      {
	for (int i = 0; i < column_infos.size(); i++)
	  {
	    size += column_infos[i].get_packed_size (serializator, size);
	  }
      }
    return size;
  }

}
