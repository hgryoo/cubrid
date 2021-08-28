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
    db_type = DB_TYPE_NULL;
    set_type = DB_TYPE_NULL;

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

  column_info::column_info (int db_type, int set_type, short scale, int prec, char charset,
			    std::string col_name, std::string default_value, char auto_increment,
			    char unique_key, char primary_key, char reverse_index, char reverse_unique,
			    char foreign_key, char shared, std::string attr_name, std::string class_name,
			    char is_non_null)
  {
    this->db_type = db_type;
    this->set_type = set_type;

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
    serializator.pack_int (db_type);
    serializator.pack_int (set_type);

    serializator.pack_int (charset);
    serializator.pack_short (scale);
    serializator.pack_int (prec);
    serializator.pack_string (col_name);
    serializator.pack_string (attr_name);
    serializator.pack_string (class_name);
    serializator.pack_string (default_value_string);

    serializator.pack_int (is_non_null);
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
    deserializator.unpack_int (db_type);
    deserializator.unpack_int (set_type);
    
    int cs, p, inn;
    deserializator.unpack_int (cs);
    deserializator.unpack_short (scale);
    deserializator.unpack_int (p);

    deserializator.unpack_string (col_name);
    deserializator.unpack_string (attr_name);
    deserializator.unpack_string (class_name);
    deserializator.unpack_string (default_value_string);
    
    deserializator.unpack_int (inn);

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
    size_t size = serializator.get_packed_int_size (size); // db_type
    size += serializator.get_packed_int_size (size); // set_type

    size += serializator.get_packed_int_size (start_offset); // charset
    size += serializator.get_packed_short_size (size); // scale
    size += serializator.get_packed_int_size (size); // prec

    size += serializator.get_packed_string_size (col_name, size); // col_name
    size += serializator.get_packed_string_size (attr_name, size); // attr_name
    size += serializator.get_packed_string_size (class_name, size); // class_name
    size += serializator.get_packed_string_size (default_value_string, size); // default_value_string

    size += serializator.get_packed_int_size (size); // is_non_null
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
  column_info::dump ()
  {
      fprintf (stdout, "class_name: %s\n", class_name.c_str());
      fprintf (stdout, "attr_name: %s\n", attr_name.c_str());
      fprintf (stdout, "col_name: %s\n", col_name.c_str());
      fprintf (stdout, "default_value_string: %s\n", default_value_string.c_str());

      fprintf (stdout, "scale: %d\n", scale);
      fprintf (stdout, "prec: %d\n", prec);

      fprintf (stdout, "charset: %d\n", charset);
      fprintf (stdout, "is_non_null: %d\n", is_non_null);


      fprintf (stdout, "auto_increment: %d\n", auto_increment);
      fprintf (stdout, "unique_key: %d\n", unique_key);
      fprintf (stdout, "primary_key: %d\n", primary_key);
      fprintf (stdout, "reverse_index: %d\n", reverse_index);
      fprintf (stdout, "reverse_unique: %d\n", reverse_unique);
      fprintf (stdout, "foreign_key: %d\n", foreign_key);
      fprintf (stdout, "shared: %d\n", shared);
  }

  void
  prepare_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (handle_id);
    serializator.pack_int ((int) stmt_type);
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
    column_infos.resize (num_column_info);
	for (int i = 0; i < num_column_info; i++)
	  {
        column_infos[i].unpack (deserializator);
	  }
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

  void
  prepare_info::dump ()
  {
      fprintf (stdout, "handler_id: %d\n", handle_id);
      fprintf (stdout, "stmt_type: %d\n", stmt_type);
      fprintf (stdout, "num_markers: %d\n", num_markers);
      fprintf (stdout, "column_infos.size(): %d\n", column_infos.size());
      fprintf (stdout, "==============================");
      for (int i = 0; i < column_infos.size(); i++)
      {
          fprintf (stdout, "=> column_infos[%d]:\n", i);
          column_infos[i].dump();
      }
      fprintf (stdout, "==============================");
  }


  void
  execute_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (qresult_infos.size());
  }

  void
  execute_info::unpack (cubpacking::unpacker &deserializator)
  {

  }

  size_t
  execute_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // num_affected
    size += serializator.get_packed_int_size (size); // num_q_result
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

  void
  execute_info::dump ()
  {

  }

}
