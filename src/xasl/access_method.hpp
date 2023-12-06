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
// access_method.hpp - defines structures required to access method spec type.
//

#ifndef _ACCESS_METHOD_NODE_HPP_
#define _ACCESS_METHOD_NODE_HPP_

#include <string>
#include <vector>

#include "method_def.hpp"
#include "object_domain.h"

// forward declarations
struct xasl_node;
class regu_variable_node;
struct regu_variable_list_node;

namespace cubxasl
{
  struct method_spec_node
  {
    struct regu_variable_list_node *method_regu_list;	/* regulator variable list */
    struct xasl_node *xasl;		/* the XASL node that contains the */
    /* list file ID for the method */
    /* arguments */
    struct method_sig_list *method_sig_list;	/* method signature list */

    method_spec_node ()
    : method_regu_list {nullptr}
    , xasl {nullptr}
    , method_sig_list {nullptr}
    {
        //
    }
    void init () {};
    void clear_xasl () {};
  };
};

using METHOD_SPEC_TYPE = cubxasl::method_spec_node;

#endif