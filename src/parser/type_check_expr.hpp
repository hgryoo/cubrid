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
 * type_check_expr.hpp
 */

#ifndef _TYPE_CHECK_EXPR_HPP_
#define _TYPE_CHECK_EXPR_HPP_

#include "parse_type.hpp"

struct parser_context;
struct parser_node;

/* SQL expression signature */
struct expression_signature
{
  pt_arg_type return_type {PT_TYPE_NONE};
  pt_arg_type arg1_type {PT_TYPE_NONE};
  pt_arg_type arg2_type {PT_TYPE_NONE};
  pt_arg_type arg3_type {PT_TYPE_NONE};
};

using EXPRESSION_SIGNATURE = struct expression_signature;
using expr_all_signatures = std::vector<expression_signature>;

/* SQL expression definition (set of overloads) */
struct expression_definitions
{
  PT_OP_TYPE op;
  expr_all_signatures sigs;
};

using EXPRESSION_DEFINITION = struct expression_definitions;

typedef struct compare_between_operator
{
  PT_OP_TYPE left;
  PT_OP_TYPE right;
  PT_OP_TYPE between;
} COMPARE_BETWEEN_OPERATOR;

COMPARE_BETWEEN_OPERATOR pt_get_compare_between_operator_table (int i);
int pt_get_compare_between_operator_count ();

bool pt_get_expression_definition (const PT_OP_TYPE op, expression_definitions &def);

bool pt_is_range_comp_op (PT_OP_TYPE op);
bool pt_is_range_expression (const PT_OP_TYPE op);
bool pt_is_able_to_determine_return_type (const PT_OP_TYPE op);
bool pt_is_op_with_forced_common_type (PT_OP_TYPE op);
bool pt_is_range_or_comp (PT_OP_TYPE op);
bool pt_is_op_w_collation (const PT_OP_TYPE op);
bool pt_is_symmetric_op (PT_OP_TYPE op);
bool pt_is_enumeration_special_comparison (PT_NODE *arg1, PT_OP_TYPE op, PT_NODE *arg2);

#endif
