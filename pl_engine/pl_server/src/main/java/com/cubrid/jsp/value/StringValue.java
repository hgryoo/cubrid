/*
 * Copyright (C) 2008 Search Solution Corporation.
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

package com.cubrid.jsp.value;

import com.cubrid.jsp.exception.TypeMismatchException;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.time.ZonedDateTime;

public class StringValue extends Value {
    private byte[] value;

    public StringValue(String value) {
        super();
        this.value = value.getBytes ();
    }

    public StringValue(byte[] value) {
        super();
        this.value = value;
    }

    public StringValue(String value, int mode, int dbType) {
        super(mode);
        this.value = value.getBytes ();
        this.dbType = dbType;
    }

    public StringValue(byte[] value, int mode, int dbType) {
        super(mode);
        this.value = value;
        this.dbType = dbType;
    }

    public byte toByte() throws TypeMismatchException {
        if (value.length == 1) {
                return value[0];
        } else {
                throw new TypeMismatchException();
        }
    }

    public short toShort() throws TypeMismatchException {
        try {
                return Short.parseShort(new String(value));
            } catch (NumberFormatException e) {
                throw new TypeMismatchException(e.getMessage());
            }
    }

    public int toInt() throws TypeMismatchException {
        try {
            return Integer.parseInt(new String(value));
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public long toLong() throws TypeMismatchException {
        try {
            return Long.parseLong(new String(value));
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public float toFloat() throws TypeMismatchException {
        try {
            return Float.parseFloat(new String(value));
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public double toDouble() throws TypeMismatchException {
        try {
            return Double.parseDouble(new String(value));
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Byte toByteObject() throws TypeMismatchException {
        return toByte ();
    }

    public Short toShortObject() throws TypeMismatchException {
        return toShort ();
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        return toInt ();
    }

    public Long toLongObject() throws TypeMismatchException {
        return toLong ();
    }

    public Float toFloatObject() throws TypeMismatchException {
        return toFloat ();
    }

    public Double toDoubleObject() throws TypeMismatchException {
        try {
            return toDouble ();
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Date toDate() throws TypeMismatchException {
        LocalDate lDate = DateTimeParser.DateLiteral.parse(new String(value));
        if (lDate == null) {
            throw new TypeMismatchException("invalid DATE string: " + value);
        } else if (lDate.equals(DateTimeParser.nullDate)) {
            return new Date(0 - 1900, 0 - 1, 0); // 0000-00-00
        } else {
            return Date.valueOf(lDate);
        }
    }

    public Time toTime() throws TypeMismatchException {
        LocalTime lTime = DateTimeParser.TimeLiteral.parse(new String(value));
        if (lTime == null) {
            throw new TypeMismatchException("invalid TIME string: " + value);
        } else {
            return Time.valueOf(lTime);
        }
    }

    public Timestamp toTimestamp() throws TypeMismatchException {
        ZonedDateTime lTimestamp = DateTimeParser.TimestampLiteral.parse(new String(value));
        if (lTimestamp == null) {
            throw new TypeMismatchException("invalid TIMESTAMP string: " + value);
        } else if (lTimestamp.equals(DateTimeParser.nullDatetimeUTC)) {
            return new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0); // 0000-00-00 00:00:00
        } else {
            return Timestamp.valueOf(lTimestamp.toLocalDateTime());
        }
    }

    public Timestamp toDatetime() throws TypeMismatchException {
        LocalDateTime lDatetime = DateTimeParser.DatetimeLiteral.parse(new String(value));
        if (lDatetime == null) {
            throw new TypeMismatchException("invalid DATETIME string: " + value);
        } else if (lDatetime.equals(DateTimeParser.nullDatetime)) {
            return new Timestamp(0 - 1900, 0 - 1, 0, 0, 0, 0, 0); // 0000-00-00 00:00:00.000
        } else {
            return Timestamp.valueOf(lDatetime);
        }
    }

    public BigDecimal toBigDecimal() throws TypeMismatchException {
        try {
            return new BigDecimal(new String(value));
        } catch (NumberFormatException e) {
            throw new TypeMismatchException(e.getMessage());
        }
    }

    public Object toObject() throws TypeMismatchException {
        return toString();
    }

    public String toString() {
        return new String (value);
    }

    public byte[] toByteArray() throws TypeMismatchException {
        return value;
    }

    public short[] toShortArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public int[] toIntegerArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public long[] toLongArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public float[] toFloatArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public double[] toDoubleArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public BigDecimal[] toBigDecimalArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Date[] toDateArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Time[] toTimeArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[] toTimestampArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Timestamp[] toDatetimeArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public String[] toStringArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Byte[] toByteObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Double[] toDoubleObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Float[] toFloatObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Integer[] toIntegerObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Long[] toLongObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }

    public Short[] toShortObjArray() throws TypeMismatchException {
        throw new TypeMismatchException();
    }
}
