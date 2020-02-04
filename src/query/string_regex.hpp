/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// string_regex - definitions and functions related to regular expression
//

#ifndef _STRING_REGEX_HPP_
#define _STRING_REGEX_HPP_

#ifdef __cplusplus
#include <regex>
#include <string>

#include "error_manager.h"

namespace cubregex
{
  /* it throws the error_collate when collatename syntax ([[. .]]), which gives an inconsistent result, is detected. */
  struct cub_reg_traits : std::regex_traits<char>
  {
    template< class Iter >
    string_type lookup_collatename ( Iter first, Iter last ) const
    {
      throw std::regex_error (std::regex_constants::error_collate);
    }
  };

  typedef std::regex_iterator<std::string::iterator, char, cub_reg_traits> cub_regex_iterator;
  typedef std::match_results <std::string::iterator> cub_regex_results;

  class compiled_regex
  {
    public:

      std::basic_regex <char, cub_reg_traits> *reg;
      char *pattern;

      compiled_regex ();
      ~compiled_regex ();

      void set (std::basic_regex <char, cub_reg_traits> *&reg, char *&pattern);
      void set (compiled_regex &&regex);
      void swap (compiled_regex &regex);

      void clear ();

      bool is_set ();

      bool search (std::string &src);

      std::string replace (const std::string &src, const std::string &repl);
      std::string replace (const std::string &src, const std::string &repl, const int position);
      std::string replace (const std::string &src, const std::string &repl, const int position, const int occurrence);
  };

  inline void clear_regex (char *&compiled_pattern, std::basic_regex<char, cub_reg_traits> *&compiled_regex);

  /* because regex_error::what() gives different messages depending on compiler, an error message should be returned by error code of regex_error explicitly. */
  std::string parse_regex_exception (std::regex_error &e);

  int parse_match_type (const std::string &opt_str, std::regex_constants::syntax_option_type &reg_flags);

  int compile_regex (const std::string &pattern_str, const std::regex_constants::syntax_option_type &reg_flags,
		     compiled_regex &regex);

  int compile_regex_internal (const std::basic_string<char> &pattern_str,
			      std::basic_regex<char, cub_reg_traits> *&rx_compiled_regex,
			      const std::regex_constants::syntax_option_type &reg_flags);
}

using COMPILED_REGEX_OBJECT = std::basic_regex <char, cubregex::cub_reg_traits>;
using COMPILED_REGEX = cubregex::compiled_regex;
#else
typedef void COMPILED_REGEX;
#endif

#endif // _STRING_REGEX_HPP_
