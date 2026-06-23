/*
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
 * stream_session.hpp - Server-side stream-session interface
 *
 * Abstract consumer of a client->server byte stream. The common transport
 * (network handlers, per-session slot) drives this interface; concrete
 * consumers (e.g. COPY FROM STDIN) implement it.
 */

#ifndef _STREAM_SESSION_HPP_
#define _STREAM_SESSION_HPP_

#include "thread_compat.hpp"

class stream_session
{
  public:
    virtual ~stream_session () = default;

    virtual int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) = 0;
    virtual int finish (THREAD_ENTRY *thread_p, int *result) = 0;
    virtual void abort (THREAD_ENTRY *thread_p) = 0;
};

#endif /* _STREAM_SESSION_HPP_ */
