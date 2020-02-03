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

#include "string_regex.hpp"

#include "error_manager.h"
#include "memory_alloc.h"
#include <string>

namespace cubregex
{
  compiled_regex::compiled_regex() : reg (NULL), pattern (NULL)
  {}

  compiled_regex::~compiled_regex()
  {
    clear_regex (pattern, reg);
  }

  void compiled_regex::set (std::basic_regex <char, cub_reg_traits> *&r, char *&p)
  {
    this->reg = std::move (r);
    this->pattern = std::move (p);
    r = NULL;
    p = NULL;
  }

  void compiled_regex::set (compiled_regex &&regex)
  {
    set (regex.reg, regex.pattern);
  }

  void compiled_regex::clear ()
  {
    clear_regex (pattern, reg);
  }

  bool compiled_regex::is_set ()
  {
    if (reg != NULL && pattern != NULL)
      {
	return true;
      }
    return false;
  }

  bool compiled_regex::search (std::string &src)
  {
    return std::regex_search (src, * (this->reg));
  }

  std::string compiled_regex::replace (const std::string &src, const std::string &repl)
  {
    return std::regex_replace (src, * (this->reg), repl);
  }

  std::string compiled_regex::replace (const std::string &src, const std::string &repl, const int position)
  {
    assert (position >= 0 && position < src.size ());

    /* split source string by position value */
    std::string prefix (src.substr (0, position));
    std::string target (src.substr (position, src.size () - position));

    std::string result = replace (target, repl);
    return prefix.append (result);
  }

  std::string compiled_regex::replace (const std::string &src, const std::string &repl, const int position,
				       const int occurrence)
  {
    assert (position >= 0 && position < src.size ());
    assert (occurrence >= 0);

    /* split source string by position value */
    std::string prefix (src.substr (0, position));
    std::string target (src.substr (position, src.size () - position));

    /* perform regular expression according to the occurence value */
    std::string result;
    if (occurrence == 0)
      {
	result.assign (replace (target, repl));
      }
    else
      {
	auto reg_iter = cub_regex_iterator (target.begin (), target.end (), * (this->reg));
	auto reg_end = cub_regex_iterator ();
	auto last_iter = reg_iter;
	int n = occurrence;
	auto out = std::back_inserter (result);
	while (reg_iter != reg_end)
	  {
	    last_iter = reg_iter;
	    std::string match_prefix = reg_iter->prefix ().str ();
	    out = std::copy (match_prefix.begin (), match_prefix.end (), out);
	    if (n == 0)
	      {
		out = reg_iter->format (out, repl);
	      }
	    else
	      {
		std::string match_str = reg_iter->str ();
		out = std::copy (match_str.begin (), match_str.end (), out);
	      }
	    ++reg_iter;
	  }

	if (last_iter != reg_end)
	  {
	    std::string match_suffix = last_iter->suffix ().str ();
	    out = std::copy (match_suffix.begin (), match_suffix.end (), out);
	  }
      }

    return prefix.append (result);
  }

  void
  clear_regex (char *&compiled_pattern, std::basic_regex<char, cub_reg_traits> *&compiled_regex)
  {
    if (compiled_pattern != NULL)
      {
	db_private_free_and_init (NULL, compiled_pattern);
      }

    if (compiled_regex != NULL)
      {
	delete compiled_regex;
	compiled_regex = NULL;
      }
  }

  std::string
  parse_regex_exception (std::regex_error &e)
  {
    std::string error_message;
    using namespace std::regex_constants;
    switch (e.code ())
      {
      case error_collate:
	error_message.assign ("regex_error(error_collate): the expression contains an invalid collating element name");
	break;
      case error_ctype:
	error_message.assign ("regex_error(error_ctype): the expression contains an invalid character class name");
	break;
      case error_escape:
	error_message.assign ("regex_error(error_escape): the expression contains an invalid escaped character or a trailing escape");
	break;
      case error_backref:
	error_message.assign ("regex_error(error_backref): the expression contains an invalid back reference");
	break;
      case error_brack:
	error_message.assign ("regex_error(error_brack): the expression contains mismatched square brackets ('[' and ']')");
	break;
      case error_paren:
	error_message.assign ("regex_error(error_paren): the expression contains mismatched parentheses ('(' and ')')");
	break;
      case error_brace:
	error_message.assign ("regex_error(error_brace): the expression contains mismatched curly braces ('{' and '}')");
	break;
      case error_badbrace:
	error_message.assign ("regex_error(error_badbrace): the expression contains an invalid range in a {} expression");
	break;
      case error_range:
	error_message.assign ("regex_error(error_range): the expression contains an invalid character range (e.g. [b-a])");
	break;
      case error_space:
	error_message.assign ("regex_error(error_space): there was not enough memory to convert the expression into a finite state machine");
	break;
      case error_badrepeat:
	error_message.assign ("regex_error(error_badrepeat): one of *?+{ was not preceded by a valid regular expression");
	break;
      case error_complexity:
	error_message.assign ("regex_error(error_complexity): the complexity of an attempted match exceeded a predefined level");
	break;
      case error_stack:
	error_message.assign ("regex_error(error_stack): there was not enough memory to perform a match");
	break;
      default:
	error_message.assign ("regex_error(error_unknown)");
	break;
      }
    return error_message;
  }

  int
  parse_match_type (const std::string &opt_str, std::regex_constants::syntax_option_type &reg_flags)
  {
    int error_status = NO_ERROR;

    auto mt_iter = opt_str.begin ();
    while ((mt_iter != opt_str.end ()) && (error_status == NO_ERROR))
      {
	char opt = *mt_iter;
	switch (opt)
	  {
	  case 'c':
	    reg_flags &= ~std::regex_constants::icase;
	    break;
	  case 'i':
	    reg_flags |= std::regex_constants::icase;
	    break;
	  default:
	    error_status = ER_QPROC_INVALID_PARAMETER;
	    break;
	  }
	++mt_iter;
      }

    return error_status;
  }

  int
  compile_regex (const std::string &pattern_str, const std::regex_constants::syntax_option_type &reg_flags,
		 compiled_regex &regex)
  {
    int error_status = NO_ERROR;
    bool should_compile = false;

    COMPILED_REGEX_OBJECT *compiled_regex = regex.reg;
    char *compiled_pattern = regex.pattern;

    /* regex must be recompiled if regex object is not specified or different flags are set */
    if (compiled_regex == NULL || reg_flags != compiled_regex->flags ())
      {
	should_compile = true;
      }

    /* regex must be recompiled if pattern is not specified or compiled pattern does not match current pattern */
    int pattern_length = pattern_str.size ();
    const char *pattern_char_p = pattern_str.c_str ();
    if (compiled_pattern == NULL || pattern_length != strlen (compiled_pattern)
	|| strncmp (compiled_pattern, pattern_char_p, pattern_length) != 0)
      {
	should_compile = true;
      }

    /* update compiled pattern */
    if (should_compile == true)
      {
	regex.clear ();

	/* allocate new memory */
	compiled_pattern = (char *) db_private_alloc (NULL, pattern_length + 1);
	if (compiled_pattern == NULL)
	  {
	    /* out of memory */
	    return ER_OUT_OF_VIRTUAL_MEMORY;
	  }

	/* copy string */
	memcpy (compiled_pattern, pattern_char_p, pattern_length);
	compiled_pattern[pattern_length] = '\0';

	error_status = compile_regex_internal (compiled_pattern, compiled_regex, reg_flags);
	regex.set (compiled_regex, compiled_pattern);
	if (error_status != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	  }
      }

    return error_status;
  }

  int compile_regex_internal (const std::basic_string<char> &pattern_str,
			      std::basic_regex<char, cub_reg_traits> *&rx_compiled_regex,
			      const std::regex_constants::syntax_option_type &reg_flags)
  {
    {
      int error_status = NO_ERROR;
      try
	{
#if defined(WINDOWS)
	  /* HACK: collating element features doesn't work well on Windows.
	  *  And lookup_collatename is not invoked when regex pattern has collating element.
	  *  It is hacky code finding collating element pattern and throw error.
	  */
	  CharT *collate_elem_pattern = "[[.";
	  int found = pattern_str.find ( std::string (collate_elem_pattern));
	  if (found != std::string::npos)
	    {
	      throw std::regex_error (std::regex_constants::error_collate);
	    }
#endif
	  rx_compiled_regex = new std::basic_regex<char, cub_reg_traits> (pattern_str, reg_flags);
	  if (rx_compiled_regex == NULL)
	    {
	      error_status = ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      catch (std::regex_error &e)
	{
	  // regex compilation exception
	  error_status = ER_REGEX_COMPILE_ERROR;
	  std::string error_message = parse_regex_exception (e);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 1, error_message.c_str ());
	}

      return error_status;
    }
  }

  /*
  template int compile_regex_internal <char, cub_reg_traits> (const std::basic_string<char> *pattern_str,
      std::basic_regex<char, cub_reg_traits> *&rx_compiled_regex,
      std::regex_constants::syntax_option_type &reg_flags);
  */

}