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

import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.DateValue;
import com.cubrid.jsp.value.DatetimeValue;
import com.cubrid.jsp.value.DoubleValue;
import com.cubrid.jsp.value.FloatValue;
import com.cubrid.jsp.value.IntValue;
import com.cubrid.jsp.value.LongValue;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.NumericValue;
import com.cubrid.jsp.value.OidValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.ShortValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.TimeValue;
import com.cubrid.jsp.value.TimestampValue;
import com.cubrid.jsp.value.Value;
import cubrid.jdbc.jci.UConnection;
import java.io.DataInputStream;
import java.io.IOException;
import java.sql.SQLException;
import java.util.Calendar;

public class Packer {

    /* DB Types */
    public static final int DB_NULL = 0;
    public static final int DB_INT = 1;
    public static final int DB_FLOAT = 2;
    public static final int DB_DOUBLE = 3;
    public static final int DB_STRING = 4;
    public static final int DB_OBJECT = 5;
    public static final int DB_SET = 6;
    public static final int DB_MULTISET = 7;
    public static final int DB_SEQUENCE = 8;
    public static final int DB_TIME = 10;
    public static final int DB_TIMESTAMP = 11;
    public static final int DB_DATE = 12;
    public static final int DB_MONETARY = 13;
    public static final int DB_SHORT = 18;
    public static final int DB_NUMERIC = 22;
    public static final int DB_CHAR = 25;
    public static final int DB_RESULTSET = 28;
    public static final int DB_BIGINT = 31;
    public static final int DB_DATETIME = 32;

    public static Value[] readArguments(DataInputStream dis, int paramCount)
            throws IOException, TypeMismatchException, SQLException {
        Value[] args = new Value[paramCount];

        for (int i = 0; i < paramCount; i++) {
            int mode = dis.readInt();
            int dbType = dis.readInt();
            int paramType = dis.readInt();
            int paramSize = dis.readInt();

            Value arg = readArgument(dis, paramSize, paramType, mode, dbType);
            args[i] = (arg);
        }

        return args;
    }

    public static Value[] readArgumentsForSet(DataInputStream dis, int paramCount)
            throws IOException, TypeMismatchException, SQLException {
        Value[] args = new Value[paramCount];

        for (int i = 0; i < paramCount; i++) {
            int paramType = dis.readInt();
            int paramSize = dis.readInt();
            Value arg = readArgument(dis, paramSize, paramType, Value.IN, 0);
            args[i] = (arg);
        }

        return args;
    }

    public static Value readArgument(
            DataInputStream dis, int paramSize, int paramType, int mode, int dbType)
            throws IOException, TypeMismatchException, SQLException {
        Value arg = null;
        switch (paramType) {
            case DB_SHORT:
                // assert paramSize == 4
                arg = new ShortValue((short) dis.readInt(), mode, dbType);
                break;
            case DB_INT:
                // assert paramSize == 4
                arg = new IntValue(dis.readInt(), mode, dbType);
                break;
            case DB_BIGINT:
                // assert paramSize == 8
                arg = new LongValue(dis.readLong(), mode, dbType);
                break;
            case DB_FLOAT:
                // assert paramSize == 4
                arg = new FloatValue(dis.readFloat(), mode, dbType);
                break;
            case DB_DOUBLE:
            case DB_MONETARY:
                // assert paramSize == 8
                arg = new DoubleValue(dis.readDouble(), mode, dbType);
                break;
            case DB_NUMERIC:
                {
                    byte[] paramValue = new byte[paramSize];
                    dis.readFully(paramValue);

                    int i;
                    for (i = 0; i < paramValue.length; i++) {
                        if (paramValue[i] == 0) break;
                    }

                    byte[] strValue = new byte[i];
                    System.arraycopy(paramValue, 0, strValue, 0, i);

                    arg = new NumericValue(new String(strValue), mode, dbType);
                }
                break;
            case DB_CHAR:
            case DB_STRING:
                // assert paramSize == n
                {
                    byte[] paramValue = new byte[paramSize];
                    dis.readFully(paramValue);

                    int i;
                    for (i = 0; i < paramValue.length; i++) {
                        if (paramValue[i] == 0) break;
                    }

                    byte[] strValue = new byte[i];
                    System.arraycopy(paramValue, 0, strValue, 0, i);
                    arg = new StringValue(new String(strValue), mode, dbType);
                }
                break;
            case DB_DATE:
                // assert paramSize == 3
                {
                    int year = dis.readInt();
                    int month = dis.readInt();
                    int day = dis.readInt();

                    arg = new DateValue(year, month, day, mode, dbType);
                }
                break;
            case DB_TIME:
                // assert paramSize == 3
                {
                    int hour = dis.readInt();
                    int min = dis.readInt();
                    int sec = dis.readInt();
                    Calendar cal = Calendar.getInstance();
                    cal.set(0, 0, 0, hour, min, sec);

                    arg = new TimeValue(hour, min, sec, mode, dbType);
                }
                break;
            case DB_TIMESTAMP:
                // assert paramSize == 6
                {
                    int year = dis.readInt();
                    int month = dis.readInt();
                    int day = dis.readInt();
                    int hour = dis.readInt();
                    int min = dis.readInt();
                    int sec = dis.readInt();
                    Calendar cal = Calendar.getInstance();
                    cal.set(year, month, day, hour, min, sec);

                    arg = new TimestampValue(year, month, day, hour, min, sec, mode, dbType);
                }
                break;
            case DB_DATETIME:
                // assert paramSize == 7
                {
                    int year = dis.readInt();
                    int month = dis.readInt();
                    int day = dis.readInt();
                    int hour = dis.readInt();
                    int min = dis.readInt();
                    int sec = dis.readInt();
                    int msec = dis.readInt();
                    Calendar cal = Calendar.getInstance();
                    cal.set(year, month, day, hour, min, sec);

                    arg = new DatetimeValue(year, month, day, hour, min, sec, msec, mode, dbType);
                }
                break;
            case DB_SET:
            case DB_MULTISET:
            case DB_SEQUENCE:
                {
                    int nCol = dis.readInt();
                    // System.out.println(nCol);
                    arg = new SetValue(readArgumentsForSet(dis, nCol), mode, dbType);
                }
                break;
            case DB_OBJECT:
                {
                    int page = dis.readInt();
                    short slot = (short) dis.readInt();
                    short vol = (short) dis.readInt();

                    byte[] bOID = new byte[UConnection.OID_BYTE_SIZE];
                    bOID[0] = ((byte) ((page >>> 24) & 0xFF));
                    bOID[1] = ((byte) ((page >>> 16) & 0xFF));
                    bOID[2] = ((byte) ((page >>> 8) & 0xFF));
                    bOID[3] = ((byte) ((page >>> 0) & 0xFF));
                    bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
                    bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
                    bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
                    bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

                    arg = new OidValue(bOID, mode, dbType);
                }
                break;
            case DB_NULL:
                arg = new NullValue(mode, dbType);
                break;
            default:
                // unknown type
                break;
        }
        return arg;
    }
}
