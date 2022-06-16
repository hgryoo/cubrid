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

#include "method_error_handler.hpp"

#include <cstring>

namespace cubmethod
{
  //////////////////////////////////////////////////////////////////////////
  // Packable error object
  //////////////////////////////////////////////////////////////////////////
  error_packable::error_packable (error_type err_type, int err_id, const char *err_msg, const char *err_file, int err_line)
  {
    type = static_cast<int> (err_type);
    set_error (err_id, err_msg, err_file, err_line);
  }

  error_packable::error_packable (error_type err_type, const cuberr::er_message &cub_err)
  : error_packable (err_type, cub_err.err_id, cub_err.msg_area, cub_err.file_name, cub_err.line_no)
  {
    //
  }

  bool
  error_packable::has_error ()
  {
    return (id != NO_ERROR);
  }

  void
  error_packable::clear ()
  {
    type = static_cast<int> (error_type::NONE);
    id = NO_ERROR;
    msg.clear ();
    file_name.clear ();
    line_no = 0;
  }

  int
  error_packable::get_error_id () const
  {
    return id;
  }

  std::string
  error_packable::get_error_msg () const
  {
    return msg;
  }

  int
  error_packable::get_error_line () const
  {
    return line_no;
  }

  std::string
  error_packable::get_error_file_name () const
  {
    return file_name;
  }

  void
  error_packable::set_error (int err_id, const char *err_msg, const char *err_file, int err_line)
  {
    id = err_id;
    msg.assign (err_msg ? err_msg : "");
    file_name.assign (err_file ? err_file : "");
    line_no = err_line;
  }

#define ERROR_CONTEXT_ARGS() \
  type, id, msg, file_name, line_no

  void
  error_packable::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (ERROR_CONTEXT_ARGS ());
  }

  void
  error_packable::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (ERROR_CONTEXT_ARGS ());
  }

  size_t
  error_packable::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, ERROR_CONTEXT_ARGS ());
  }

  //////////////////////////////////////////////////////////////////////////
  // Error handler
  //////////////////////////////////////////////////////////////////////////
  error_handler::error_handler ()
  : m_type (error_type::NONE)
  , m_err_msg (false)
  {
    //
  }

  void 
  error_handler::set_error_dbms (bool clear)
  {
    if (er_has_error () == false)
    {
      return;
    }

    m_type = error_type::DBMS;

    const cuberr::context &tl_context = cuberr::context::get_thread_local_context ();
    cuberr::er_message& current_error = tl_context.get_thread_local_error ();

    // clear current error
    m_err_msg.clear_message_area ();
    m_err_msg.clear_error ();

    // swap, current_error will be reset
    m_err_msg.swap (current_error);

    // clear thread local error
    if (clear)
    {
      er_clear ();
    }
  }

  void 
  error_handler::set_error_dbms (const char *file_name, const int line_no, int err_id, int num_args, ...)
  {
    if (err_id == NO_ERROR)
    {
      return;
    }

    m_type = error_type::DBMS;

    int severity = ER_ERROR_SEVERITY;

    const cuberr::context &tl_context = cuberr::context::get_thread_local_context ();

    // clear current error
    m_err_msg.clear_message_area ();
    m_err_msg.clear_error ();

    // save error_manager's error
    cuberr::er_message saved_error (false);
    saved_error.swap (tl_context.get_thread_local_error ());

    // set legacy thread local error
    // TODO: separate from error_manager.c
    va_list ap;
    va_start (ap, num_args);
    (void) er_set (severity, file_name, line_no, err_id, num_args, ap);
    va_end (ap);

    // swap er_message with thread local error and clear the thread local error
    m_err_msg.swap (tl_context.get_thread_local_error ());
    er_clear ();

    // restore saved error
    tl_context.get_thread_local_error ().swap (saved_error);
  }
  
#if !defined (SERVER_MODE)
  void 
  error_handler::set_error_cas (const char *file_name, const int line_no, error_code_cas err_code)
  {
    m_type = error_type::CAS;

    int severity = ER_ERROR_SEVERITY;
    m_err_msg.set_error (static_cast<int> (err_code), severity, file_name, line_no);
  }
#endif

  void 
  error_handler::set_error_msg (const std::string& msg)
  {
    if (msg.empty ())
    {
      return;
    }

    m_err_msg.clear_message_area (); // clear current error's message area
    m_err_msg.reserve_message_area (msg.size () + 1);
    std::strncpy (m_err_msg.msg_area, msg.data (), m_err_msg.msg_area_size);
  }

  void 
  error_handler::clear ()
  {
    m_type = error_type::NONE;

    // clear error
    m_err_msg.clear_message_area ();
    m_err_msg.clear_error ();
  }

  bool
  error_handler::has_error (void) const
  {
    return m_type != error_type::NONE;
  }

  cuberr::er_message &
  error_handler::get_error ()
  {
    return m_err_msg;
  }

  error_packable
  error_handler::get_packable_error ()
  {
    error_packable ep (m_type, m_err_msg);
    return ep;
  }

} // namespace cubmethod
