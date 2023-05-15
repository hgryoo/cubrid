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
 * expr_type.hpp
 */

#ifndef _EXPR_TYPE_HPP_
#define _EXPR_TYPE_HPP_

#include "parse_type.hpp"

struct parser_context;
struct parser_node;

/* maximum number of overloads for an expression */
#define MAX_OVERLOADS 16

/* SQL expression signature */
typedef struct expression_signature
{
  PT_ARG_TYPE return_type;
  PT_ARG_TYPE arg1_type;
  PT_ARG_TYPE arg2_type;
  PT_ARG_TYPE arg3_type;
} EXPRESSION_SIGNATURE;

/* SQL expression definition */
typedef struct expression_definition
{
  PT_OP_TYPE op;
  int overloads_count;
  EXPRESSION_SIGNATURE overloads[MAX_OVERLOADS];
} EXPRESSION_DEFINITION;


typedef struct compare_between_operator
{
  PT_OP_TYPE left;
  PT_OP_TYPE right;
  PT_OP_TYPE between;
} COMPARE_BETWEEN_OPERATOR;

COMPARE_BETWEEN_OPERATOR pt_get_compare_between_operator_table (int i);
int pt_get_compare_between_operator_count ();

namespace expr_type
{
  
}

bool pt_is_range_comp_op (PT_OP_TYPE op);
bool pt_is_range_expression (const PT_OP_TYPE op);
bool pt_is_able_to_determine_return_type (const PT_OP_TYPE op);
bool pt_is_op_with_forced_common_type (PT_OP_TYPE op);
bool pt_is_range_or_comp (PT_OP_TYPE op);
bool pt_is_op_w_collation (const PT_OP_TYPE op);
bool pt_is_symmetric_op (PT_OP_TYPE op);

#endif
