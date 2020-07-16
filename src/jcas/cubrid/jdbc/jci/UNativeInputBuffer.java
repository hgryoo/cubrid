/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.IOException;
import java.sql.Date;
import java.sql.Time;
import java.util.Calendar;
import java.util.TimeZone;

import org.cubrid.CAS;

import cubrid.jdbc.driver.CUBRIDBinaryString;
import cubrid.jdbc.driver.CUBRIDBlob;
import cubrid.jdbc.driver.CUBRIDClob;
import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDXid;
import cubrid.sql.CUBRIDOID;
import cubrid.sql.CUBRIDTimestamp;
import cubrid.sql.CUBRIDTimestamptz;

public class UNativeInputBuffer extends UInputBuffer {
	private CAS.T_NET_BUF.ByReference netBuf;

	public UNativeInputBuffer(CAS.T_NET_BUF.ByReference netBuf, UConnection con)
			throws IOException, UJciException {
		position = 8;
		capacity = netBuf.data_size + 8;
		uconn = con;
		this.netBuf = netBuf;
		
		casinfo = new byte[4];
		casinfo[0] = 1; // CAS_INFO_STATUS
		casinfo[1] = -1; // CAS_INFO_RESERVED_1
		casinfo[2] = -1; // CAS_INFO_RESERVED_2
		casinfo[3] = -1; // CAS_INFO_ADDITIONAL_FLAG
		/*
		capacity = netBuf.data_size;
		
		position = 8;
		//buffer = netBuf.data.getByteArray(8, capacity);
		resCode = readInt();

		if (capacity <= 0) {
			resCode = 0;
			capacity = 0;
			return;
		}
		
		capacity += 8;
		
		System.out.println ("data size = " + capacity);
		System.out.println ("alloc size = " + netBuf.alloc_size);
		System.out.println ("res code = " + resCode);

		if (resCode < 0) {
			int eCode = readInt();
			String msg;
			
			if (con.isRenewedSessionId()) {
				byte[] newSessionId = new byte[20];
				
				msg = readString(remainedCapacity() - newSessionId.length, 
						 UJCIManager.sysCharsetName);
				readBytes(newSessionId);
				con.setNewSessionId (newSessionId);
			} else {
				msg = readString(remainedCapacity(), 
						 UJCIManager.sysCharsetName);
			}
			
			eCode = convertErrorByVersion(resCode, eCode);
			throw uconn.createJciException(UErrorCode.ER_DBMS, resCode, eCode, msg);
		}
		*/
	}

	int convertErrorByVersion(int indicator, int error) {
	    if (!uconn.protoVersionIsSame(UConnection.PROTOCOL_V2)
		    && !uconn.brokerInfoRenewedErrorCode()) {
		if (indicator == UErrorCode.CAS_ERROR_INDICATOR
			|| error == UErrorCode.CAS_ER_NOT_AUTHORIZED_CLIENT) {
		    // old error converts to new error
		    return error - 9000;
		}
	    }

	    return error;
	}

	byte[] getCasInfo() {
		return casinfo;
	}

	int getResCode() {
		return resCode;
	}

	byte readByte() throws UJciException {
		if (position >= capacity) {
			throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		
		return netBuf.data.getByte(position++);

		//return buffer[position++];
	}

	void readBytes(byte value[], int offset, int len) throws UJciException {
		if (value == null)
			return;

		if (position + len > capacity) {
		    throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		
		byte[] bytes = netBuf.data.getByteArray(position, len);
		
		System.arraycopy(bytes, 0, value, offset, len);
		position += len;
	}

	void readBytes(byte value[]) throws UJciException {
		readBytes(value, 0, value.length);
	}

	byte[] readBytes(int size) throws UJciException {
		byte[] value = new byte[size];
		readBytes(value, 0, size);
		return value;
	}

	double readDouble() throws UJciException {
		return Double.longBitsToDouble(readLong());
	}

	float readFloat() throws UJciException {
		return Float.intBitsToFloat(readInt());
	}

	int readInt() throws UJciException {
		if (position + 4 > capacity) {
			throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		//int data = UJCIUtil.bytes2int(buffer, position);
		//int data = netBuf.data.getByteArray(position, Integer.BYTES);
		
		//int data = UJCIUtil.bytes2int(buffer, position);
		
		byte[] barr = netBuf.data.getByteArray(position, 4);
		
		int data = UJCIUtil.bytes2int(barr, 0);
		//int data = ByteBuffer.wrap(barr).getInt();
		position += 4;

		return data;
	}
	
	String byteArrayToHex(byte[] a) {
	    StringBuilder sb = new StringBuilder();
	    for(final byte b: a)
	        sb.append(String.format("%02x ", b&0xff));
	    return sb.toString();
	}
	
	long readLong() throws UJciException {
		long data = 0;

		if (position + 8 > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		
		byte[] barr = netBuf.data.getByteArray(position, 8);
		
		
		for (int i = 0; i < 8; i++) {
			data <<= 8;
			data |= (barr[i++] & 0xff);
		}

		return data;
	}

	short readShort() throws UJciException {
		if (position + 2 > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		
		byte[] barr = netBuf.data.getByteArray(position, 2);
		
		short data = UJCIUtil.bytes2short(barr, 0);
		position += 2;

		return data;
	}

	String readString(int size, String charsetName) throws UJciException {
		String stringData;

		if (size <= 0)
			return null;

		if (position + size > capacity) {
				System.out.println ("position: " + position);
				System.out.println ("size: " + size);
				System.out.println ("capacity: " + capacity);
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		
		byte[] b = netBuf.data.getByteArray(position, size - 1);
		if (charsetName != null) {
			try {
				stringData = new String(b, charsetName);
			} catch (java.io.UnsupportedEncodingException e) {
				stringData = new String(b);
			}
		} else {
			stringData = new String(b);
		}

		position += size;

		return stringData;
	}

	CUBRIDBinaryString readBinaryString(int size) throws UJciException {
		byte[] byteArray;

		if (size <= 0)
			return null;

		if (position + size > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
		byteArray = netBuf.data.getByteArray(position, size - 1);
		//byteArray = java.util.Arrays.copyOfRange (buffer, position, position + size - 1);
		
		CUBRIDBinaryString stringData = new CUBRIDBinaryString (byteArray);

		position += size;

		return stringData;
	}

	Date readDate() throws UJciException {
		int year, month, day;
		year = readShort();
		month = readShort();
		day = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_ILLEGAL_TIMESTAMP);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (year == 0 && month == 0 && day == 0) {
			cal.set(0, 0, 1, 0, 0, 0); /* round to 0001-01-01 00:00:00) */
		} else {
			cal.set(year, month - 1, day, 0, 0, 0);
		}
		cal.set(Calendar.MILLISECOND, 0);

		return new Date(cal.getTimeInMillis());
	}

	Time readTime() throws UJciException {
		int hour, minute, second;
		hour = readShort();
		minute = readShort();
		second = readShort();

		Calendar cal = Calendar.getInstance();
		cal.set(1970, 0, 1, hour, minute, second);
		cal.set(Calendar.MILLISECOND, 0);

		return new Time(cal.getTimeInMillis());
	}

	CUBRIDTimestamp readTimestamp(boolean is_tz) throws UJciException {
		int year, month, day, hour, minute, second;
		year = readShort();
		month = readShort();
		day = readShort();
		hour = readShort();
		minute = readShort();
		second = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_ILLEGAL_TIMESTAMP);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (is_tz) {
			cal.setTimeZone(TimeZone.getTimeZone("UTC"));
		}
		if (year == 0 && month == 0 && day == 0) {
			cal.setTimeInMillis(0); /* round to 1970-01-01 00:00:00 UTC */
		} else {
			cal.set(year, month - 1, day, hour, minute, second);
		}
		cal.set(Calendar.MILLISECOND, 0);

		return new CUBRIDTimestamp(cal.getTimeInMillis(),
				CUBRIDTimestamp.TIMESTAMP);
	}
	
	CUBRIDTimestamptz readTimestamptz(int size) throws UJciException {
		CUBRIDTimestamp cubrid_ts;
		String timezone;
		int tmp_position;
		int ts_size;
		
		if (position + size > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
				
		tmp_position = position;
		cubrid_ts = readTimestamp (true);
		
		ts_size = position - tmp_position;
				
		if (ts_size > 0){
			byte[] byteArray = netBuf.data.getByteArray(position, size - ts_size - 1);
			timezone = new String (byteArray);
			position += size - ts_size;
		}
		else{
			timezone = "";
			position++;
		}

		return CUBRIDTimestamptz.valueOf(cubrid_ts, timezone);
	}	

	CUBRIDTimestamp readDatetime(boolean is_tz) throws UJciException {
		int year, month, day, hour, minute, second, millisecond;
		year = readShort();
		month = readShort();
		day = readShort();
		hour = readShort();
		minute = readShort();
		second = readShort();
		millisecond = readShort();

		if (year == 0 && month == 0 && day == 0) {
			if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)) {
				throw new UJciException(UErrorCode.ER_ILLEGAL_TIMESTAMP);
			} else if (uconn.getZeroDateTimeBehavior().equals(
					UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)) {
				return null;
			}
		}

		Calendar cal = Calendar.getInstance();
		if (is_tz) {
			cal.setTimeZone(TimeZone.getTimeZone("UTC"));
		}
		if (year == 0 && month == 0 && day == 0) {
			cal.set(0, 0, 1, 0, 0, 0); /* round to 0001-01-01 00:00:00) */
		} else {
			cal.set(year, month - 1, day, hour, minute, second);
		}
		cal.set(Calendar.MILLISECOND, millisecond);

		return new CUBRIDTimestamp(cal.getTimeInMillis(),
				CUBRIDTimestamp.DATETIME);
	}
	
	CUBRIDTimestamptz readDatetimetz(int size) throws UJciException {
		CUBRIDTimestamp cubrid_ts;
		String timezone;
		int tmp_position;
		int ts_size;
		
		if (position + size > capacity) {
		    	throw uconn.createJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}
				
		tmp_position = position;
		cubrid_ts = readDatetime (true);
		
		if (cubrid_ts == null){
			/* zero datetime NULL behavior */
			return null;
		}

		ts_size = position - tmp_position;
				
		if (ts_size > 0){
			byte[] byteArray = netBuf.data.getByteArray(position, size - ts_size - 1);
			timezone = new String (byteArray);
			position += size - ts_size;
		}
		else{
			timezone = "";
			position++;
		}

		return CUBRIDTimestamptz.valueOf(cubrid_ts, timezone);
	}

	CUBRIDOID readOID(CUBRIDConnection con) throws UJciException {
		byte[] oid = readBytes(UConnection.OID_BYTE_SIZE);
		for (int i = 0; i < oid.length; i++) {
			if (oid[i] != (byte) 0) {
				return (new CUBRIDOID(con, oid));
			}
		}
		return null;
	}
	
	CUBRIDBlob readBlob(int packedLobHandleSize, CUBRIDConnection conn)
			throws UJciException {
		try {
			byte[] packedLobHandle = readBytes(packedLobHandleSize);
			return new CUBRIDBlob(conn, packedLobHandle, true);
		} catch (Exception e) {
		    	throw uconn.createJciException(UErrorCode.ER_UNKNOWN);
		}
	}

	CUBRIDClob readClob(int packedLobHandleSize, CUBRIDConnection conn)
			throws UJciException {
		try {
			byte[] packedLobHandle = readBytes(packedLobHandleSize);
			return new CUBRIDClob(conn, packedLobHandle, conn.getUConnection()
					.getCharset(), true);
		} catch (Exception e) {
		    	throw uconn.createJciException(UErrorCode.ER_UNKNOWN);
		}
	}

	int remainedCapacity() {
		return capacity - position;
	}

	CUBRIDXid readXid() throws UJciException {
		readInt(); // msg_size
		int formatId = readInt();
		int gid_size = readInt();
		int bid_size = readInt();
		byte[] gid = readBytes(gid_size);
		byte[] bid = readBytes(bid_size);

		return (new CUBRIDXid(formatId, gid, bid));
	}
}
