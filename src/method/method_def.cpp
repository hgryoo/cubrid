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

#include "method_def.hpp"

#include "memory_private_allocator.hpp"

method_sig_node::method_sig_node ()
  : method_name {}
  , method_type {METHOD_TYPE_NONE}
  , num_method_args {0}
  , method_arg_pos {}
  , class_name {}
  , arg_info {}
{
  //
}

method_sig_node::~method_sig_node ()
{
  freemem ();
}

method_sig_node::method_sig_node (method_sig_node &&obj)
{
  method_name = std::move (obj.method_name);
  method_type = obj.method_type;
  method_arg_pos = std::move (obj.method_arg_pos);
  num_method_args = obj.num_method_args;

  class_name = std::move (obj.class_name);

  arg_info.arg_mode = std::move (obj.arg_info.arg_mode);
  arg_info.arg_type = std::move (obj.arg_info.arg_type);
  arg_info.result_type = obj.arg_info.result_type;
}

void
method_sig_node::pack (cubpacking::packer &serializator) const
{
  serializator.pack_string (method_name);

  serializator.pack_int (method_type);
  serializator.pack_int (num_method_args);
  for (int i = 0; i < num_method_args + 1; i++)
    {
      serializator.pack_int (method_arg_pos[i]);
    }

  serializator.pack_string (class_name);

  for (int i = 0; i < num_method_args; i++)
    {
      serializator.pack_int (arg_info.arg_mode[i]);
    }
  for (int i = 0; i < num_method_args; i++)
    {
      serializator.pack_int (arg_info.arg_type[i]);
    }
  serializator.pack_int (arg_info.result_type);
}

size_t
method_sig_node::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  size_t size = serializator.get_packed_string_size (method_name, size); /* method_name */

  /* method type and num_method_args */
  size += serializator.get_packed_int_size (size);
  size += serializator.get_packed_int_size (size);
  for (int i = 0; i < num_method_args + 1; i++)
    {
      size += serializator.get_packed_int_size (size); /* method_sig->method_arg_pos[i] */
    }

  size += serializator.get_packed_string_size (class_name, size);

  for (int i = 0; i < num_method_args; i++)
    {
      size += serializator.get_packed_int_size (size); /* method_sig->arg_info.arg_mode[i] */
      size += serializator.get_packed_int_size (size); /* method_sig->arg_info.arg_type[i] */
    }
  size += serializator.get_packed_int_size (size); /* method_sig->arg_info.result_type */

  return size;
}

method_sig_node &
method_sig_node::operator= (method_sig_node &&obj)
{
  if (this != &obj)
    {
      method_name = std::move (obj.method_name);
      method_type = obj.method_type;
      method_arg_pos = std::move (obj.method_arg_pos);
      num_method_args = obj.num_method_args;

      class_name = std::move (obj.class_name);

      arg_info.arg_mode = std::move (obj.arg_info.arg_mode);
      arg_info.arg_type = std::move (obj.arg_info.arg_type);
      arg_info.result_type = obj.arg_info.result_type;
    }

  return *this;
}

void
method_sig_node::unpack (cubpacking::unpacker &deserializator)
{
  deserializator.unpack_string (method_name);

  int type;
  deserializator.unpack_int (type);
  method_type = static_cast<METHOD_TYPE> (type);
  deserializator.unpack_int (num_method_args);

  method_arg_pos.resize (num_method_args + 1);
  for (int n = 0; n < num_method_args + 1; n++)
    {
      deserializator.unpack_int (method_arg_pos[n]);
    }

  deserializator.unpack_string (class_name);

  arg_info.arg_mode.resize (num_method_args, 0);
  arg_info.arg_type.resize (num_method_args, 0);
  if (num_method_args > 0)
    {
      for (int i = 0; i < num_method_args; i++)
	{
	  deserializator.unpack_int (arg_info.arg_mode[i]);
	}

      for (int i = 0; i < num_method_args; i++)
	{
	  deserializator.unpack_int (arg_info.arg_type[i]);
	}
    }

  deserializator.unpack_int (arg_info.result_type);
}

void
method_sig_node::freemem ()
{
  method_name.clear ();

  method_arg_pos.clear ();

  class_name.clear ();
  arg_info.arg_mode.clear ();
  arg_info.arg_type.clear ();
}

void
method_sig_node::clear_xasl ()
{
  freemem ();
}

method_sig_list::method_sig_list ()
  : method_sigs {}
{
  //
}

method_sig_list::~method_sig_list ()
{
  freemem ();
}

method_sig_list::method_sig_list (method_sig_list &&obj)
  : method_sigs {std::move(obj.method_sigs)}
{
  //
}

method_sig_list &
method_sig_list::operator= (method_sig_list &&obj)
{
  if (this != &obj)
    {
        method_sigs = std::move (obj.method_sigs);
    }

  return *this;
}

void
method_sig_list::freemem ()
{
  method_sigs.clear ();
}

void
method_sig_list::clear_xasl ()
{
  freemem ();
}

void
method_sig_list::pack (cubpacking::packer &serializator) const
{
  int num_methods = (int) method_sigs.size ();
  serializator.pack_int (num_methods);
  for (int i = 0; i < num_methods; i++)
    {
      method_sigs[i].pack (serializator);
    }
}

size_t
method_sig_list::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
{
  size_t size = serializator.get_packed_int_size (start_offset); /* num_methods */

  int num_methods = (int) method_sigs.size ();
  for (int i = 0; i < num_methods; i++)
    {
      size += method_sigs[i].get_packed_size (serializator, size);
    }
  return size;
}

void
method_sig_list::unpack (cubpacking::unpacker &deserializator)
{
  int num_methods = 0;
  deserializator.unpack_int (num_methods);

  method_sigs.resize (num_methods);
  for (int i = 0; i < num_methods; i++)
  {
        method_sigs[i].unpack (deserializator);
  }
}
