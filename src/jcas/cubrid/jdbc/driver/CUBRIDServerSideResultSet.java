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

package cubrid.jdbc.driver;

import java.io.InputStream;
import java.io.Reader;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashMap;
import java.util.Map;

import org.cubrid.JCASWrapper;

import cubrid.jdbc.jci.UColumnInfo;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UStatement;


/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDServerSideResultSet implements ResultSet {
	
	/* JDBC */
	protected CUBRIDConnection con;
	protected CUBRIDStatement stmt;
	protected CUBRIDResultSetMetaData meta;
	
	/* JCI */
	protected UStatement uStmt;
	protected boolean closeUStmtOnClose;
	
	/* Flags */
	protected boolean isClosed;
	protected boolean completeOnClose;
	
	protected boolean wasNull;
	protected boolean isHoldable;
	
	int concurrency;
	int type;
	
	boolean inserting;
	
	/* Fetch */
	protected int fetchDirection;
	protected int fetchSize;
	
	/* Result */
	protected int numberOfRows;
	protected int currentRow;
	protected UColumnInfo[] columnInfo;
	protected HashMap<String, Integer> columnNameIndex;
	
	protected CUBRIDServerSideResultSet(CUBRIDConnection c, CUBRIDStatement s, int t,
	        int concur, boolean holdable) throws SQLException {
		
		con = c;
		stmt = s;
		uStmt = s.u_stmt;
		meta = null;
		
		//numberOfRows = uStmt.getExecuteResult();
		numberOfRows = -1;

		currentRow = -1;
		columnInfo = uStmt.getColumnInfo();
		columnNameIndex = uStmt.getColumnNameToIndexMap();
		
		isClosed = false;
		wasNull = false;
		completeOnClose = false;
		closeUStmtOnClose = false;
		
		//streams = new ArrayList<Object>();
		
		concurrency = concur;
		type = t;
		
		isHoldable = false;
		if (holdable && con.u_con.supportHoldableResult()) {
			isHoldable = true;
		}
		
		fetchDirection = s.getFetchDirection();
		uStmt.setFetchDirection(fetchDirection);
		
		fetchSize = s.getFetchSize();
		uStmt.setFetchSize(fetchSize);

		inserting = false;
		
		/*
		if (is_updatable) {
			updatable = new boolean[column_info.length];
			updated = new boolean[column_info.length];
			updates = new Object[column_info.length];
			main_table_name = null;
			for (int i = 0; i < column_info.length; i++) {
				updatable[i] = column_info[i].getRealColumnName().length() > 0;
				if (updatable[i]) {
					main_table_name = column_info[i].getClassName();
				}
			}
			clearCurrentRow();
		}
		*/
	}
	
	protected boolean isScrollable () {
		return (type != TYPE_FORWARD_ONLY);
	}
	
	protected boolean isUpdatable () {
		return (concurrency == CONCUR_UPDATABLE);
	}
	
	protected boolean isSensitive () {
		return (type == TYPE_SCROLL_SENSITIVE);
	}

	protected boolean isHoldable () {
		return isHoldable;
	}
	
	public boolean next() {
		if (uStmt == null)
			return false;
		
		if (numberOfRows == -1) {
			int result = JCASWrapper.cas.fr_move_cursor(getServerHandle(), 1, UStatement.CURSOR_SET);
			numberOfRows = JCASWrapper.cas.fr_get_cursor_count(getServerHandle());
			currentRow = 0;
			return true;
		}
		else {
			//clearCurrentRow();
			currentRow++;
			if (currentRow >= numberOfRows) {
				currentRow = numberOfRows;
				return false;
			}
			
			return move();
		}
	}
	
	protected boolean move() {
		int result = JCASWrapper.cas.fr_move_cursor(getServerHandle(), currentRow + 1, UStatement.CURSOR_SET);
		numberOfRows = JCASWrapper.cas.fr_get_cursor_count(getServerHandle());
		//System.out.println("numberOfRows: " + numberOfRows);
		if (result == 0)
		{
			return true;
		}
		else if (result == 1)
		{
			// end
			return false;
		}
		else
		{	// error
			return false;
		}
	}
	
	public int getServerHandle() {
		//if (uStmt == null || !uStmt.isReturnable())
		//	return 0;

		return uStmt.getServerHandle();
	}

	@Override
	public boolean absolute(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public void afterLast() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void beforeFirst() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void cancelRowUpdates() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void clearWarnings() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void close() throws SQLException {
		if (isClosed()) {
			return;
		}
		isClosed = true;
		//clearCurrentRow();
		
		if (stmt.getResultSetHoldability() == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
			//uStmt.closeCursor();
		}
		if (completeOnClose) {
			stmt.complete();
		}
		
		uStmt.closeResult();
		if (!uStmt.isReturnable()) {
			uStmt = null;
		}
		
		columnInfo = null;
		columnNameIndex = null;
		isHoldable = false;

		/*
		if (close_u_stmt_on_close) {
			u_stmt.close();
		}
		*/
	}

	@Override
	public void deleteRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public synchronized int findColumn(String columnName) throws SQLException {
		Integer index = columnNameIndex.get(columnName.toLowerCase());
		if (index == null) {
			throw con.createCUBRIDException(
			        CUBRIDJDBCErrorCode.invalid_column_name, null);
		}

		return index.intValue() + 1;
	}

	@Override
	public boolean first() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public Array getArray(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Array getArray(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getAsciiStream(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getAsciiStream(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public BigDecimal getBigDecimal(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public BigDecimal getBigDecimal(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public BigDecimal getBigDecimal(int arg0, int arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public BigDecimal getBigDecimal(String arg0, int arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getBinaryStream(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getBinaryStream(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Blob getBlob(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Blob getBlob(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public boolean getBoolean(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean getBoolean(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public byte getByte(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public byte getByte(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public byte[] getBytes(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public byte[] getBytes(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Reader getCharacterStream(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Reader getCharacterStream(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Clob getClob(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Clob getClob(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public int getConcurrency() throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public String getCursorName() throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Date getDate(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Date getDate(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Date getDate(int arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Date getDate(String arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public double getDouble(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public double getDouble(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public int getFetchDirection() throws SQLException {
		return fetchDirection;
	}

	@Override
	public int getFetchSize() throws SQLException {
		// TODO Auto-generated method stub
		return fetchSize;
	}

	@Override
	public float getFloat(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public float getFloat(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public int getHoldability() throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public int getInt(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public int getInt(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public long getLong(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public long getLong(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public ResultSetMetaData getMetaData() throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Object getObject(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Object getObject(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Object getObject(int arg0, Map<String, Class<?>> arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Object getObject(String arg0, Map<String, Class<?>> arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Ref getRef(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Ref getRef(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public int getRow() throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public short getShort(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public short getShort(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public Statement getStatement() throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}
	
	protected void beforeGetValue () {
		synchronized (uStmt) {
			uStmt.fetch();
		}
		/*
		synchronized (u_stmt) {
			u_stmt.fetch();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
		*/
		
	}
	
	@Override
	public synchronized String getString(int columnIndex) throws SQLException {
		beforeGetValue();

		Object obj;
		synchronized (uStmt) {
			obj = uStmt.getObject(columnIndex - 1);
			//error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Clob) {
			Clob clob = (Clob) obj;
			int length;
			if (clob.length() > (long) Integer.MAX_VALUE) {
				length = Integer.MAX_VALUE;
			} else {
				length = (int) clob.length();
			}
			return clob.getSubString(1, length);
		}

		String value;
		synchronized (uStmt) {
			value = uStmt.getString(columnIndex - 1);
			//error = uStmt.getRecentError();
		}

		//checkGetXXXError();
		return value;
	}

	@Override
	public synchronized String getString(String columnName) throws SQLException {
		return getString(findColumn(columnName));
	}

	@Override
	public Time getTime(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Time getTime(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Time getTime(int arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Time getTime(String arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Timestamp getTimestamp(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Timestamp getTimestamp(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Timestamp getTimestamp(int arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public Timestamp getTimestamp(String arg0, Calendar arg1) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public int getType() throws SQLException {
		// TODO Auto-generated method stub
		return type;
	}

	@Override
	public URL getURL(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public URL getURL(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getUnicodeStream(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public InputStream getUnicodeStream(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public SQLWarning getWarnings() throws SQLException {
		// TODO Auto-generated method stub
		return null;
	}

	@Override
	public void insertRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public boolean isAfterLast() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean isBeforeFirst() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}
	
	@Override
	public boolean isFirst() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean isLast() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean last() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public void moveToCurrentRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void moveToInsertRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public boolean previous() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public void refreshRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public boolean relative(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean rowDeleted() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean rowInserted() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public boolean rowUpdated() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public void setFetchDirection(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void setFetchSize(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateArray(int arg0, Array arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateArray(String arg0, Array arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateAsciiStream(int arg0, InputStream arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateAsciiStream(String arg0, InputStream arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBigDecimal(int arg0, BigDecimal arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBigDecimal(String arg0, BigDecimal arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBinaryStream(int arg0, InputStream arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBinaryStream(String arg0, InputStream arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBlob(int arg0, Blob arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBlob(String arg0, Blob arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBoolean(int arg0, boolean arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBoolean(String arg0, boolean arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateByte(int arg0, byte arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateByte(String arg0, byte arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBytes(int arg0, byte[] arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateBytes(String arg0, byte[] arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateCharacterStream(int arg0, Reader arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateCharacterStream(String arg0, Reader arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateClob(int arg0, Clob arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateClob(String arg0, Clob arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateDate(int arg0, Date arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateDate(String arg0, Date arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateDouble(int arg0, double arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateDouble(String arg0, double arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateFloat(int arg0, float arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateFloat(String arg0, float arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateInt(int arg0, int arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateInt(String arg0, int arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateLong(int arg0, long arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateLong(String arg0, long arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateNull(int arg0) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateNull(String arg0) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateObject(int arg0, Object arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateObject(String arg0, Object arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateObject(int arg0, Object arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateObject(String arg0, Object arg1, int arg2) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateRef(int arg0, Ref arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateRef(String arg0, Ref arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateRow() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateShort(int arg0, short arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateShort(String arg0, short arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateString(int arg0, String arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateString(String arg0, String arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateTime(int arg0, Time arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateTime(String arg0, Time arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateTimestamp(int arg0, Timestamp arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public void updateTimestamp(String arg0, Timestamp arg1) throws SQLException {
		// TODO Auto-generated method stub
		
	}

	@Override
	public boolean wasNull() throws SQLException {
		return wasNull;
	}
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	/* JDK 1.6 */
	public Reader getNCharacterStream(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public Reader getNCharacterStream(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public NClob getNClob(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public NClob getNClob(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public String getNString(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public String getNString(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public RowId getRowId(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public RowId getRowId(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public boolean isClosed() throws SQLException {
		return isClosed;
	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x,
	        long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream,
	        long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader,
	        long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader,
	        long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, NClob clob) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, NClob clob) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader, long length)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNString(int columnIndex, String string)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNString(String columnLabel, String string)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateRowId(int columnIndex, RowId x) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateRowId(String columnLabel, RowId x) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateSQLXML(int columnIndex, SQLXML xmlObject)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateSQLXML(String columnLabel, SQLXML xmlObject)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.7 */
	public <T> T getObject(int columnIndex, Class<T> type) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.7 */
	public <T> T getObject(String columnLabel, Class<T> type)
	        throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	
	
}
