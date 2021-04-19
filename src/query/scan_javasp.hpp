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

#ifndef _SCAN_JAVASP_HPP_
#define _SCAN_JAVASP_HPP_

#include "scan_manager.h"
#include "storage_common.h"

#include <vector>

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubscan {
    namespace javasp {
        class scanner {
            public:

            int open (cubthread::entry *thread_p);

            int next_scan (cubthread::entry *thread_p, scan_id_struct &sid, SCAN_CODE &sc);

            void end (cubthread::entry *thread_p);

            
            METHOD_SCAN_BUFFER scan_buf;	/* value array buffer */
        };
    }
}

// naming convention of SCAN_ID's
using JAVASP_SCAN_ID = cubscan::javasp::scanner;

#endif