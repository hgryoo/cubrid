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

package com.cubrid.jsp.io;
import cubrid.jdbc.jci.UError;

public class SUConnection {

    /*
    protected UError errorHandler;
    
    // UFunctionCode.GET_DB_PARAMETER
    public synchronized int getIsolationLevel() {
        errorHandler = new UError(this);

        if (lastIsolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION) {
            return lastIsolationLevel;
        }

        if (isClosed == true) {
            errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
            return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
        }
        try {
            setBeginTime();
            checkReconnect();
            if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
                return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;

            outBuffer.newRequest(output, UFunctionCode.GET_DB_PARAMETER);
            outBuffer.addInt(DB_PARAM_ISOLATION_LEVEL);

            UInputBuffer inBuffer;
            inBuffer = send_recv_msg();

            lastIsolationLevel = inBuffer.readInt();
            return lastIsolationLevel;
        } catch (UJciException e) {
            logException(e);
            e.toUError(errorHandler);
        } catch (IOException e) {
            logException(e);
            errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
        }
        return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
    }
    */
}
