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
#include "type_check_helper.hpp"

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

class type_check_expr_helper : public cubparser::type_check_helper
{
  public:
    type_check_expr_helper (parser_context *parser, parser_node *node)
      : cubparser::type_check_helper (parser, node)
      , m_is_finished (false)
    {
      assert (node && node->node_type == PT_EXPR);
    }

    int do_type_checking () override;

    bool is_finished ()
    {
      return m_is_finished;
    }

    PT_TYPE_ENUM traverse_multiargs_range_op (PT_NODE *arg2);

    // temporary public
    PT_NODE *m_arg1_hv = nullptr;
    PT_NODE *m_arg2_hv = nullptr;
    PT_NODE *m_arg3_hv = nullptr;
    PT_NODE *m_expr = nullptr;

    PT_TYPE_ENUM m_common_type = PT_TYPE_NONE;
    PT_TYPE_ENUM m_arg1_type = PT_TYPE_NONE;
    PT_TYPE_ENUM m_arg2_type = PT_TYPE_NONE;
    PT_TYPE_ENUM m_arg3_type = PT_TYPE_NONE;

  protected:
    bool m_is_finished;

    int preprocess ();

    int handle_special_enumeration_op ();
    int eval_function_holder ();
    int adjust_expression_def (PT_TYPE_ENUM &common_type, PT_NODE *expr);

    constexpr auto GET_ARG1 ()
    {
      return m_node->info.expr.arg1;
    }
    constexpr auto GET_ARG2 ()
    {
      return m_node->info.expr.arg2;
    }
    constexpr auto GET_ARG3 ()
    {
      return m_node->info.expr.arg3;
    }
    constexpr auto GET_OP ()
    {
      return m_node->info.expr.op;
    }
};

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

bool pt_is_op_unary_special_operator_on_hv (const PT_OP_TYPE op);

bool pt_is_node_unary_special_operator_on_hv (PT_NODE *node);
bool pt_is_host_var_with_maybe_type (PT_NODE *node);

bool pt_is_enumeration_special_comparison (PT_NODE *arg1, PT_OP_TYPE op, PT_NODE *arg2);
bool pt_are_unmatchable_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type);

PT_NODE *pt_fix_enumeration_comparison (PARSER_CONTEXT *parser, PT_NODE *expr);
PT_NODE *pt_select_list_to_enumeration_expr (PARSER_CONTEXT *parser, PT_NODE *data_type, PT_NODE *node);
PT_NODE *pt_node_to_enumeration_expr (PARSER_CONTEXT *parser, PT_NODE *data_type, PT_NODE *node);
PT_NODE *pt_coerce_expr_arguments (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE *arg1, PT_NODE *arg2,
				   PT_NODE *arg3, EXPRESSION_SIGNATURE sig);
int pt_coerce_expression_argument (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE **arg, const PT_TYPE_ENUM def_type,
				   PT_NODE *data_type);
PT_TYPE_ENUM pt_infer_common_type (const PT_OP_TYPE op, PT_TYPE_ENUM *arg1, PT_TYPE_ENUM *arg2, PT_TYPE_ENUM *arg3,
				   const TP_DOMAIN *expected_domain);
int pt_apply_expressions_definition (PARSER_CONTEXT *parser, PT_NODE **expr);
PT_NODE *pt_coerce_range_expr_arguments (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE *arg1, PT_NODE *arg2,
    PT_NODE *arg3,
    EXPRESSION_SIGNATURE sig);
PT_TYPE_ENUM pt_expr_get_return_type (PT_NODE *expr, const EXPRESSION_SIGNATURE sig);
PT_NODE *pt_wrap_expr_w_exp_dom_cast (PARSER_CONTEXT *parser, PT_NODE *expr);

#endif
