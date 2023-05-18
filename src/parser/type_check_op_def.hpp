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
 * type_check_op_def.hpp
 */

#ifndef _TYPE_CHECK_EXPR_HPP_
#define _TYPE_CHECK_EXPR_HPP_

#include "parse_tree.h"

bool pt_is_range_comp_op (PT_OP_TYPE op);
bool pt_is_range_expression (const PT_OP_TYPE op);
bool pt_is_able_to_determine_return_type (const PT_OP_TYPE op);
bool pt_is_op_with_forced_common_type (PT_OP_TYPE op);
bool pt_is_range_or_comp (PT_OP_TYPE op);
bool pt_is_op_w_collation (const PT_OP_TYPE op);
bool pt_is_symmetric_op (PT_OP_TYPE op);
bool pt_is_enumeration_special_comparison (PT_NODE *arg1, PT_OP_TYPE op, PT_NODE *arg2);

#endif