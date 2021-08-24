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

#include "method_query_cursor.hpp"

#include "list_file.h"
#include "log_impl.h"
#include "query_manager.h"

namespace cubmethod
{
    query_cursor::query_cursor (THREAD_ENTRY * thread_p, QUERY_ID query_id)
    {
        m_thread = thread_p;

        int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
        QMGR_QUERY_ENTRY *query_entry_p = qmgr_get_query_entry (thread_p, query_id, tran_index);
        if (query_entry_p == NULL)
        {
            // error
        }
    }

    int
    query_cursor::open ()
    {
        return qfile_open_list_scan (m_list_id, &m_scan_id);
    }

    void
    query_cursor::close ()
    {
        qfile_close_scan (m_thread, &m_scan_id);
    }

    SCAN_CODE 
    query_cursor::prev ()
    {
        QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
        SCAN_CODE scan_code = qfile_scan_list_prev (m_thread, &m_scan_id, &tuple_record, PEEK);
        if (scan_code == S_SUCCESS)
        {

        }
    }

    SCAN_CODE 
    query_cursor::next ()
    {
        QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
        SCAN_CODE scan_code = qfile_scan_list_next (m_thread, &m_scan_id, &tuple_record, PEEK);
        if (scan_code == S_SUCCESS)
        {

        }
    }
}

