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
 * type_check.h
 */

#ifndef _TYPE_CHECK_H_
#define _TYPE_CHECK_H_

#include "parse_tree.h"

#define PT_IS_MAYBE_TYPE_NODE(node) \
  ((node) && (node->type_enum == PT_TYPE_MAYBE))

PT_NODE *pt_eval_expr_type (PARSER_CONTEXT * parser, PT_NODE * node);
PT_NODE *pt_eval_function_type (PARSER_CONTEXT * parser, PT_NODE * node);

#endif
