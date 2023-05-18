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

/*
 * type_check_helper.hpp: define a helper to check type of expression and function
 */

#ifndef _TYPE_CHECK_HELPER_HPP_
#define _TYPE_CHECK_HELPER_HPP_

#include "parse_type.hpp"

namespace cubparser
{
  class type_check_helper
  {
    protected:
      parser_context *m_parser;
      parser_node *m_node;

    public:
      type_check_helper (parser_context *parser, parser_node *node)
	: m_parser (parser)
	, m_node (node)
      {
	//
      }

      // parser_node *get_arg (size_t index);
      virtual PT_NODE *do_type_checking () = 0;
  };
}

#endif
