/*
 * Copyright (C) 2008 Search Solution CorporationUStatement
 * Copyright (C) 2016 CUBRID Corporation
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
 * 
 */
package cubrid.jdbc.jci;

import java.io.IOException;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.HashMap;

import org.cubrid.CAS;
import org.cubrid.JCASWrapper;

import cubrid.jdbc.driver.CUBRIDOutResultSet;

/**
 * @author hgryoo
 *
 */
public class UServerSideStatement extends UStatement {

	public UServerSideStatement(UConnection relatedC, UInputBuffer inBuffer,
    boolean assign_only, String sql, byte _prepare_flag)
			throws UJciException {
		super(relatedC, inBuffer, assign_only, sql, _prepare_flag);
		// TODO Auto-generated constructor stub
		//init(relatedC, inBuffer, sql, _prepare_flag, true);
	}
	
	public UColumnInfo[] getColumnInfo() {
		return columnInfo;
	}

	public HashMap<String, Integer> getColumnNameToIndexMap() {
		return colNameToIndex;
	}
	
	public UServerSideConnection getConnection() {
		return (UServerSideConnection) relatedConnection;
	}
	
	synchronized public void fetch() {
		if (isClosed == true) {
			return;
		}
		
		/* need not to fetch */
		if (statementType == GET_BY_OID)
			return;

		UNativeInputBuffer inBuffer;
		UServerSideConnection uconn = getConnection();
		try {
			synchronized (uconn) {
				CAS.T_NET_BUF.ByReference netBuf = getConnection().getBuffer();
				byte fetch_flag = (isSensitive == true) ? (byte) 1 : (byte) 0;
				int result = JCASWrapper.cas.fr_peek_cursor(getServerHandle(), (char) fetch_flag, netBuf);
				inBuffer = new UNativeInputBuffer (netBuf, uconn);
			}
			
			fetchData(inBuffer, UFunctionCode.FETCH);
			realFetched = true;
		}
		catch (UJciException e) {
			uconn.logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
			uconn.logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}
	
	@Override
	protected void fetchResultData(UInputBuffer inBuffer) throws UJciException {
		//executeResult = inBuffer.getResCode();
		executeResult = JCASWrapper.cas.fr_get_cursor_count(getServerHandle());
		totalTupleNumber = executeResult;
		batchParameter = null;
		/*
		executeResult = JCASWrapper.cas.fr_get_cursor_count(getServerHandle());
		
		//if (maxFetchSize > 0) {
		//	executeResult = Math.min(maxFetchSize, executeResult);
		//}
		totalTupleNumber = executeResult;
		batchParameter = null;

		if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_SELECT
		        && totalTupleNumber > 0) {
			//inBuffer.readInt(); // fetch_rescode
			JCASWrapper.cas.fr_move_cursor(getServerHandle(), 1, UStatement.CURSOR_SET);
			
			CAS.T_NET_BUF.ByReference netBuf = getConnection().getBuffer();
			UNativeInputBuffer buf;
			try {
				byte fetch_flag = (isSensitive == true) ? (byte) 1 : (byte) 0;
				int result = JCASWrapper.cas.fr_peek_cursor(getServerHandle(), (char) fetch_flag, netBuf);
				buf = new UNativeInputBuffer (netBuf, getConnection());
				fetchData(buf, UFunctionCode.FETCH);
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			}
		}
		*/
		
	}
	
	private void fetchData(UInputBuffer inBuffer, UFunctionCode functionCode)
	        throws UJciException {
		if (tuples == null) {
			tuples = new UResultTuple[1];
		}
		
		readATuple(0, inBuffer);

		if (functionCode == UFunctionCode.GET_GENERATED_KEYS) {
			isFetchCompleted = true;
		}
	}
	
	@Override
	synchronized public Object getObject(int index) {
		Object obj = beforeGetXXX(index);
		if (obj == null)
			return null;

		Object retValue;
		try {
			if ((commandTypeIs != CUBRIDCommandType.CUBRID_STMT_CALL_SP)
			        && (columnInfo[index].getColumnType() == UUType.U_TYPE_BIT)
			        && (columnInfo[index].getColumnPrecision() == 8)) {
				retValue = new Boolean(UGetTypeConvertedValue.getBoolean(obj));
			} else if (obj instanceof CUBRIDArray)
				retValue = ((CUBRIDArray) obj).getArrayClone();
			else if (obj instanceof byte[])
				retValue = ((byte[]) obj).clone();
			else if (obj instanceof Date)
				retValue = ((Date) obj).clone();
			else if (obj instanceof Time)
				retValue = ((Time) obj).clone();
			else if (obj instanceof Timestamp)
			{
				if (relatedConnection.getResultWithCUBRIDTypes().equals(
					UConnection.RESULT_WITH_CUBRID_TYPES_NO))
						retValue = new Timestamp(((Timestamp) obj).getTime());
				else
						retValue = ((Timestamp) obj).clone();
			}
			else if (obj instanceof CUBRIDOutResultSet) {
				try {
					((CUBRIDOutResultSet) obj).createInstance();
					retValue = obj;
				} catch (Exception e) {
					retValue = null;
				}
			} else
				retValue = obj;
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return null;
		}

		return retValue;
	}
	
	@Override
	protected Object beforeGetXXX(int index) {
		if (isClosed == true) {
			return null;
		}
		
		if (index < 0 || index >= columnNumber) {
			errorHandler.setErrorCode(UErrorCode.ER_COLUMN_INDEX);
			return null;
		}
		
		//if (fetchedTupleNumber <= 0) {
		//	errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
		//	return null;
		//}

		/*
		 * if (tuples == null || tuples[cursorPosition - currentFirstCursor] ==
		 * null || tuples[cursorPosition-currentFirstCursor].wasNull(index) ==
		 * true)
		 */
		Object obj;
		if ((tuples == null)
		        || (tuples[0] == null)
		        || ((obj = tuples[0]
		                .getAttribute(index)) == null)) {
			errorHandler.setErrorCode(UErrorCode.ER_WAS_NULL);
			return null;
		}

		return obj;
	}

}
