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
#include "parse_type.hpp"

#define PT_IS_MAYBE_TYPE_NODE(node) \
  ((node) && (node->type_enum == PT_TYPE_MAYBE))

#define PT_ARE_COMPARABLE_CHAR_TYPE(typ1, typ2)	\
   ((PT_IS_SIMPLE_CHAR_STRING_TYPE (typ1) && PT_IS_SIMPLE_CHAR_STRING_TYPE (typ2)) \
    || (PT_IS_NATIONAL_CHAR_STRING_TYPE (typ1) && PT_IS_NATIONAL_CHAR_STRING_TYPE (typ2)))

#define PT_ARE_COMPARABLE_NUMERIC_TYPE(typ1, typ2) \
   ((PT_IS_NUMERIC_TYPE (typ1) && PT_IS_NUMERIC_TYPE (typ2)) \
    || (PT_IS_NUMERIC_TYPE (typ1) && typ2 == PT_TYPE_MAYBE) \
    || (typ1 == PT_TYPE_MAYBE && PT_IS_NUMERIC_TYPE (typ2)))

/* Two types are comparable if they are NUMBER types or same CHAR type */
#define PT_ARE_COMPARABLE(typ1, typ2) \
  ((typ1 == typ2) || PT_ARE_COMPARABLE_CHAR_TYPE(typ1, typ2) || PT_ARE_COMPARABLE_NUMERIC_TYPE (typ1, typ2))

/* Two types are comparable if they are NUMBER types without CHAR type */
#define PT_ARE_COMPARABLE_NO_CHAR(typ1, typ2) \
  ((typ1 == typ2) || PT_ARE_COMPARABLE_NUMERIC_TYPE (typ1, typ2))

#define PT_IS_RECURSIVE_EXPRESSION(node) \
  ((node)->node_type == PT_EXPR && (PT_IS_LEFT_RECURSIVE_EXPRESSION (node) || PT_IS_RIGHT_RECURSIVE_EXPRESSION (node)))

#define PT_IS_LEFT_RECURSIVE_EXPRESSION(node) \
  ((node)->info.expr.op == PT_GREATEST || (node)->info.expr.op == PT_LEAST || (node)->info.expr.op == PT_COALESCE)

#define PT_IS_RIGHT_RECURSIVE_EXPRESSION(node) \
  ((node)->info.expr.op == PT_CASE || (node)->info.expr.op == PT_DECODE)

#define PT_NODE_IS_SESSION_VARIABLE(node)   				      \
  ((((node) != NULL) &&							      \
    ((node)->node_type == PT_EXPR) &&					      \
    (((node)->info.expr.op == PT_EVALUATE_VARIABLE) ||			      \
    (((node)->info.expr.op == PT_CAST) &&				      \
    ((node)->info.expr.arg1 != NULL) &&					      \
    ((node)->info.expr.arg1->node_type == PT_EXPR) &&			      \
    ((node)->info.expr.arg1->info.expr.op == PT_EVALUATE_VARIABLE))	      \
    )) ? true : false )

#define PT_COLL_WRAP_TYPE_FOR_MAYBE(type) \
  ((PT_IS_CHAR_STRING_TYPE (type)) ? (type) : PT_TYPE_VARCHAR)

PT_NODE *pt_eval_expr_type (PARSER_CONTEXT * parser, PT_NODE * node);
PT_NODE *pt_eval_function_type (PARSER_CONTEXT * parser, PT_NODE * node);

bool pt_is_symmetric_type (const PT_TYPE_ENUM type_enum);

PT_TYPE_ENUM pt_common_type_op (PT_TYPE_ENUM t1, PT_OP_TYPE op, PT_TYPE_ENUM t2);
PT_TYPE_ENUM pt_get_common_arg_type_of_width_bucket (PARSER_CONTEXT * parser, PT_NODE * node);
PT_TYPE_ENUM pt_get_equivalent_type_with_op (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM arg_type, PT_OP_TYPE op);
PT_NODE *pt_propagate_types (PARSER_CONTEXT * parser, PT_NODE * expr, PT_NODE * otype1, PT_NODE * otype2);
int pt_upd_domain_info (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2, PT_OP_TYPE op,
			PT_TYPE_ENUM common_type, PT_NODE * node);

PT_TYPE_ENUM pt_get_common_collection_type (const PT_NODE * set, bool * is_multitype);
#endif
