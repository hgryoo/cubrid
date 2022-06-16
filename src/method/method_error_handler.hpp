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
// method_error_handler.hpp
//


#ifndef _METHOD_ERROR_HPP_
#define _METHOD_ERROR_HPP_

#include <string>

#if !defined (SERVER_MODE)
#include "cas_error.h"
#endif

#include "method_def.hpp"
#include "method_error_handler.hpp"

#include "error_manager.h"
#include "error_context.hpp"

namespace cubmethod
{
  enum class error_type
  {
NONE, //
CAS, // CAS error specified at cas.h
DBMS, // DBMS error specified at error_code.h
OTHERS
  };

  struct error_packable : public cubpacking::packable_object
  {
    public:
      error_packable (error_type type, int err_id, const char *err_msg, const char *err_file, int err_line);
      error_packable (error_type type, const cuberr::er_message &cub_err);
      
      // error_packable (const error_packable &) = delete; // no copy constructor
      // error_packable &operator= (const error_packable &other) = delete; // no copy assignment operator

      bool has_error ();
      void clear ();

      int get_error_id () const;
      std::string get_error_msg () const;
      int get_error_line () const;
      std::string get_error_file_name () const;

      void set_error (int err_id, const char *err_msg, const char *err_file, int err_line);

      void pack (cubpacking::packer &serializator) const override;
      void unpack (cubpacking::unpacker &deserializator) override;
      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    private:
      int type;
      int id;
      std::string msg;
      std::string file_name;
      int line_no;
  };

#if !defined (SERVER_MODE)
  // it defines error codes that only used in the method module
  enum class error_code_cas : int
  {
    METHOD_ER_INTERNAL = CAS_ER_INTERNAL,
    METHOD_ER_OBJECT = CAS_ER_OBJECT,
    METHOD_ER_INVALID_CALL_STMT = CAS_ER_INVALID_CALL_STMT,
    METHOD_ER_NOT_COLLECTION = CAS_ER_NOT_COLLECTION,
    METHOD_ER_COLLECTION_DOMAIN = CAS_ER_COLLECTION_DOMAIN,
    METHOD_ER_NOT_IMPLEMENTED = CAS_ER_NOT_IMPLEMENTED
  };
#endif

  class error_handler 
  {
    public:
      error_handler ();
      ~error_handler () = default; // Default destructor

      error_handler (error_handler &&other) = delete; // Not MoveConstructible
      error_handler (const error_handler &copy) = delete; // Not CopyConstructible

      error_handler &operator= (error_handler &&other) = delete; // Not MoveAssignable
      error_handler &operator= (const error_handler &copy) = delete; // Not CopyAssignable

      void set_error_dbms (bool clear = true); // set error from thread local error module
      void set_error_dbms (const char *file_name, const int line_no, int err_id, int num_args, ...);

#if !defined (SERVER_MODE)
      void set_error_cas (const char *file_name, const int line_no, error_code_cas err_code);
#endif

      void set_error_msg (const std::string& msg);

      void set_interrupt ();

      bool has_error () const;
      void clear ();

      cuberr::er_message & get_error ();
      error_packable get_packable_error ();

    private:
      error_type m_type;
      cuberr::er_message m_err_msg;
  };
}

#endif
