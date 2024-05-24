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

package com.cubrid.jsp;

import java.nio.ByteBuffer;

import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.PackableObject;
import com.cubrid.jsp.protocol.PrepareArgs;
import com.cubrid.jsp.value.Value;

public class RequestAPI {

        public static CUBRIDUnpacker getUnpacker (Context ctx) throws InterruptedException {
                CUBRIDUnpacker unpacker = ctx.getCurrentExecuteThread().getUnpacker();
                unpacker.setBuffer(ctx.getInboundQueue().take());
                return unpacker;
        }

        // RequestCode.PREPARE_ARGS
        public static PackableObject prepare (Context ctx) throws Exception {
                CUBRIDUnpacker unpacker = getUnpacker (ctx);
                PrepareArgs args = new PrepareArgs(unpacker);
                ctx.getPrepareArgsStack().push(args);
                ctx.checkTranId(args.getTranId());
                return null;
        }

        // RequestCode.INVOKE_SP
        public static PackableObject invoke (Context ctx) throws Exception {
                CUBRIDUnpacker unpacker = getUnpacker (ctx);
                long id = unpacker.unpackBigint();
                int tid = unpacker.unpackInt();
                ctx.checkTranId(tid);

                StoredProcedure procedure = new StoredProcedure (unpacker);
                ctx.getProcedureStack ().push (procedure);

                pushUser(procedure.getAuthUser());
                Value result = procedure.invoke();
                popUser();

                return result;
        }

        // RequestCode.COMPILE
        public static PackableObject compile (Context ctx, ByteBuffer input) {
                return null;
        }


}
