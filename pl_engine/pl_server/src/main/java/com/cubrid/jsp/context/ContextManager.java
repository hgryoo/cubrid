/*
 *
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.context;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

public class ContextManager {

    // singleton
    private ContextManager() {
        //
    }

    // Context ID => Context Object
    private static ConcurrentMap<Long, Context> contextMap = new ConcurrentHashMap<Long, Context>();

    public static boolean hasContext(long id) {
        return contextMap.containsKey(id);
    }

    public static Context getContext(long id) {
        if (hasContext(id)) {
            return contextMap.get(id);
        } else {
            synchronized (ContextManager.class) {
                Context newCtx = new Context(id);
                contextMap.put(id, newCtx);
                return newCtx;
            }
        }
    }

    // Java Thread ID => Context ID
    private static ConcurrentMap<Long, Long> contextThreadMap = new ConcurrentHashMap<Long, Long>();

    public static void registerThread(long threadId, long ctxId) {
        if (contextThreadMap.containsKey(threadId) == false) {
            contextThreadMap.put(threadId, ctxId);
        }
    }

    public static void deregisterThread(long threadId) {
        if (contextThreadMap.containsKey(threadId) == true) {
            contextThreadMap.remove(threadId);
        }
    }

    public static Long getContextIdByThreadId(long threadId) {
        if (contextThreadMap.containsKey(threadId)) {
            return contextThreadMap.get(threadId);
        }
        return null;
    }

    public static Context getContextofCurrentThread() {
        Thread t = Thread.currentThread();
        Long ctxId = ContextManager.getContextIdByThreadId(t.getId());
        return ContextManager.getContext(ctxId);
    }
}
