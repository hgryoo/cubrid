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

package com.cubrid.jsp.jdbc;

import cubrid.sql.CUBRIDOID;
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
import java.util.Calendar;
import java.util.Map;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideResultSet implements ResultSet {

    CUBRIDServerSideStatement stmt;

    boolean isHoldable;

    protected CUBRIDServerSideResultSet() throws SQLException {}

    /*
     * java.sql.ResultSet interface
     */

    public boolean next() throws SQLException {
        // TODO
        return true;
    }

    public void close() throws SQLException {
        // TODO
    }

    public synchronized boolean wasNull() throws SQLException {
        // TODO
        return true;
    }

    public synchronized String getString(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized boolean getBoolean(int columnIndex) throws SQLException {
        // TODO
        return true;
    }

    public synchronized byte getByte(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public synchronized short getShort(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public synchronized int getInt(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public synchronized long getLong(int columnIndex) throws SQLException {
        // TODO
        return 0;
    }

    public synchronized float getFloat(int columnIndex) throws SQLException {
        // TODO
        return 0.0f;
    }

    public synchronized double getDouble(int columnIndex) throws SQLException {
        // TODO
        return 0.0f;
    }

    public BigDecimal getBigDecimal(int columnIndex, int scale) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized byte[] getBytes(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized Date getDate(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized Time getTime(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized Timestamp getTimestamp(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized InputStream getAsciiStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public InputStream getUnicodeStream(int columnIndex) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized InputStream getBinaryStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized String getString(String columnName) throws SQLException {
        return getString(findColumn(columnName));
    }

    public synchronized boolean getBoolean(String columnName) throws SQLException {
        return getBoolean(findColumn(columnName));
    }

    public synchronized byte getByte(String columnName) throws SQLException {
        return getByte(findColumn(columnName));
    }

    public synchronized short getShort(String columnName) throws SQLException {
        return getShort(findColumn(columnName));
    }

    public synchronized int getInt(String columnName) throws SQLException {
        return getInt(findColumn(columnName));
    }

    public synchronized long getLong(String columnName) throws SQLException {
        return getLong(findColumn(columnName));
    }

    public synchronized float getFloat(String columnName) throws SQLException {
        return getFloat(findColumn(columnName));
    }

    public synchronized double getDouble(String columnName) throws SQLException {
        return getDouble(findColumn(columnName));
    }

    public BigDecimal getBigDecimal(String columnName, int scale) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized byte[] getBytes(String columnName) throws SQLException {
        return getBytes(findColumn(columnName));
    }

    public synchronized Date getDate(String columnName) throws SQLException {
        return getDate(findColumn(columnName));
    }

    public synchronized Time getTime(String columnName) throws SQLException {
        return getTime(findColumn(columnName));
    }

    public synchronized Timestamp getTimestamp(String columnName) throws SQLException {
        return getTimestamp(findColumn(columnName));
    }

    public synchronized InputStream getAsciiStream(String columnName) throws SQLException {
        return getAsciiStream(findColumn(columnName));
    }

    public InputStream getUnicodeStream(String columnName) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized InputStream getBinaryStream(String columnName) throws SQLException {
        return getBinaryStream(findColumn(columnName));
    }

    public synchronized SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    public synchronized void clearWarnings() throws SQLException {
        /* do nothing */
    }

    public synchronized String getCursorName() throws SQLException {
        /* do nothing */
        return "";
    }

    public synchronized ResultSetMetaData getMetaData() throws SQLException {
        // TODO
        return null;
    }

    public synchronized Object getObject(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized Object getObject(String columnName) throws SQLException {
        return getObject(findColumn(columnName));
    }

    //
    public synchronized int findColumn(String columnName) throws SQLException {
        // TODO
        return 0;
        /*
        Integer index = col_name_to_index.get(columnName.toLowerCase());
        if (index == null) {
            throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_column_name, null);
        }

        return index.intValue() + 1;
        */
    }

    public synchronized Reader getCharacterStream(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized Reader getCharacterStream(String columnName) throws SQLException {
        return getCharacterStream(findColumn(columnName));
    }

    public synchronized BigDecimal getBigDecimal(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public synchronized BigDecimal getBigDecimal(String columnName) throws SQLException {
        return getBigDecimal(findColumn(columnName));
    }

    public synchronized boolean isBeforeFirst() throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean isAfterLast() throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean isFirst() throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean isLast() throws SQLException {
        // TODO
        return true;
    }

    public synchronized void beforeFirst() throws SQLException {
        // TODO
    }

    public synchronized void afterLast() throws SQLException {
        // TODO
    }

    public synchronized boolean first() throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean last() throws SQLException {
        // TODO
        return true;
    }

    public synchronized int getRow() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized boolean absolute(int row) throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean relative(int rows) throws SQLException {
        // TODO
        return true;
    }

    public synchronized boolean previous() throws SQLException {
        // TODO
        return true;
    }

    public synchronized void setFetchDirection(int direction) throws SQLException {
        // TODO
    }

    public synchronized int getFetchDirection() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized void setFetchSize(int rows) throws SQLException {
        // TODO
    }

    public synchronized int getFetchSize() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized int getType() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized int getConcurrency() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized boolean rowUpdated() throws SQLException {
        // TODO
        return false;
    }

    public synchronized boolean rowInserted() throws SQLException {
        // TODO
        return false;
    }

    public synchronized boolean rowDeleted() throws SQLException {
        // TODO
        boolean b = false;
        return !b;
    }

    private void updateValue(int columnIndex, Object value) throws SQLException {
        // TODO
    }

    public synchronized void updateNull(int columnIndex) throws SQLException {
        updateValue(columnIndex, null);
    }

    public synchronized void updateBoolean(int columnIndex, boolean x) throws SQLException {
        updateValue(columnIndex, new Boolean(x));
    }

    public synchronized void updateByte(int columnIndex, byte x) throws SQLException {
        updateValue(columnIndex, new Byte(x));
    }

    public synchronized void updateShort(int columnIndex, short x) throws SQLException {
        updateValue(columnIndex, new Short(x));
    }

    public synchronized void updateInt(int columnIndex, int x) throws SQLException {
        updateValue(columnIndex, new Integer(x));
    }

    public synchronized void updateLong(int columnIndex, long x) throws SQLException {
        updateValue(columnIndex, new Long(x));
    }

    public synchronized void updateFloat(int columnIndex, float x) throws SQLException {
        updateValue(columnIndex, new Float(x));
    }

    public synchronized void updateDouble(int columnIndex, double x) throws SQLException {
        updateValue(columnIndex, new Double(x));
    }

    public synchronized void updateBigDecimal(int columnIndex, BigDecimal x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateString(int columnIndex, String x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateBytes(int columnIndex, byte[] x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateDate(int columnIndex, Date x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateTime(int columnIndex, Time x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateTimestamp(int columnIndex, Timestamp x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateAsciiStream(int columnIndex, InputStream x, int length)
            throws SQLException {
        // TODO
        // updateValue(columnIndex, new String(value, 0, len));
    }

    public synchronized void updateBinaryStream(int columnIndex, InputStream x, int length)
            throws SQLException {
        // TODO
    }

    public synchronized void updateCharacterStream(int columnIndex, Reader x, int length)
            throws SQLException {
        // TODO
    }

    public synchronized void updateObject(int columnIndex, Object x, int scale)
            throws SQLException {
        try {
            updateObject(columnIndex, new BigDecimal(((Number) x).toString()).setScale(scale));
        } catch (SQLException e) {
            throw e;
        } catch (Exception e) {
            updateObject(columnIndex, x);
        }
    }

    public synchronized void updateObject(int columnIndex, Object x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateNull(String columnName) throws SQLException {
        updateNull(findColumn(columnName));
    }

    public synchronized void updateBoolean(String columnName, boolean x) throws SQLException {
        updateBoolean(findColumn(columnName), x);
    }

    public synchronized void updateByte(String columnName, byte x) throws SQLException {
        updateByte(findColumn(columnName), x);
    }

    public synchronized void updateShort(String columnName, short x) throws SQLException {
        updateShort(findColumn(columnName), x);
    }

    public synchronized void updateInt(String columnName, int x) throws SQLException {
        updateInt(findColumn(columnName), x);
    }

    public synchronized void updateLong(String columnName, long x) throws SQLException {
        updateLong(findColumn(columnName), x);
    }

    public synchronized void updateFloat(String columnName, float x) throws SQLException {
        updateFloat(findColumn(columnName), x);
    }

    public synchronized void updateDouble(String columnName, double x) throws SQLException {
        updateDouble(findColumn(columnName), x);
    }

    public synchronized void updateBigDecimal(String columnName, BigDecimal x) throws SQLException {
        updateBigDecimal(findColumn(columnName), x);
    }

    public synchronized void updateString(String columnName, String x) throws SQLException {
        updateString(findColumn(columnName), x);
    }

    public synchronized void updateBytes(String columnName, byte[] x) throws SQLException {
        updateBytes(findColumn(columnName), x);
    }

    public synchronized void updateDate(String columnName, Date x) throws SQLException {
        updateDate(findColumn(columnName), x);
    }

    public synchronized void updateTime(String columnName, Time x) throws SQLException {
        updateTime(findColumn(columnName), x);
    }

    public synchronized void updateTimestamp(String columnName, Timestamp x) throws SQLException {
        updateTimestamp(findColumn(columnName), x);
    }

    public synchronized void updateAsciiStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateAsciiStream(findColumn(columnName), x, length);
    }

    public synchronized void updateBinaryStream(String columnName, InputStream x, int length)
            throws SQLException {
        updateBinaryStream(findColumn(columnName), x, length);
    }

    public synchronized void updateCharacterStream(String columnName, Reader reader, int length)
            throws SQLException {
        updateCharacterStream(findColumn(columnName), reader, length);
    }

    public synchronized void updateObject(String columnName, Object x, int scale)
            throws SQLException {
        updateObject(findColumn(columnName), x, scale);
    }

    public synchronized void updateObject(String columnName, Object x) throws SQLException {
        updateObject(findColumn(columnName), x);
    }

    public synchronized void insertRow() throws SQLException {
        // TODO
    }

    public synchronized void updateRow() throws SQLException {
        // TODO
    }

    public synchronized void deleteRow() throws SQLException {
        // TODO
    }

    public synchronized void refreshRow() throws SQLException {
        // TODO
    }

    public synchronized void cancelRowUpdates() throws SQLException {
        // TODO
    }

    public synchronized void moveToInsertRow() throws SQLException {
        // TODO
    }

    public synchronized void moveToCurrentRow() throws SQLException {
        // TODO
    }

    public synchronized Statement getStatement() throws SQLException {
        return stmt;
    }

    public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Ref getRef(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized Blob getBlob(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Clob getClob(int columnIndex) throws SQLException {
        // TODO
        return null;
    }

    public Array getArray(int i) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Object getObject(String colName, Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Ref getRef(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public Blob getBlob(String colName) throws SQLException {
        return (getBlob(findColumn(colName)));
    }

    public Clob getClob(String colName) throws SQLException {
        return (getClob(findColumn(colName)));
    }

    public Array getArray(String colName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized Date getDate(int columnIndex, Calendar cal) throws SQLException {
        return getDate(columnIndex);
    }

    public synchronized Date getDate(String columnName, Calendar cal) throws SQLException {
        return getDate(columnName);
    }

    public synchronized Time getTime(int columnIndex, Calendar cal) throws SQLException {
        return getTime(columnIndex);
    }

    public synchronized Time getTime(String columnName, Calendar cal) throws SQLException {
        return getTime(columnName);
    }

    public synchronized Timestamp getTimestamp(int columnIndex, Calendar cal) throws SQLException {
        return getTimestamp(columnIndex);
    }

    public synchronized Timestamp getTimestamp(String columnName, Calendar cal)
            throws SQLException {
        return getTimestamp(columnName);
    }

    // 3.0
    public synchronized URL getURL(int columnIndex) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized URL getURL(String columnName) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized void updateArray(int columnIndex, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized void updateArray(String columnName, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized void updateBlob(int columnIndex, Blob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateBlob(String columnName, Blob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    public synchronized void updateClob(int columnIndex, Clob x) throws SQLException {
        updateValue(columnIndex, x);
    }

    public synchronized void updateClob(String columnName, Clob x) throws SQLException {
        updateValue(findColumn(columnName), x);
    }

    public synchronized void updateRef(int columnIndex, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public synchronized void updateRef(String columnName, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    // 3.0

    public synchronized CUBRIDOID getOID(int columnIndex) throws SQLException {
        // TODO
        CUBRIDOID value = null;
        return value;
    }

    public synchronized CUBRIDOID getOID(String columnName) throws SQLException {
        return getOID(findColumn(columnName));
    }

    public synchronized Object getCollection(int columnIndex) throws SQLException {
        // TODO
        Object value = null;
        return value;
    }

    public synchronized Object getCollection(String columnName) throws SQLException {
        return getCollection(findColumn(columnName));
    }

    /**
     * Returns a <code>CUBRIDOID</code> object that represents the OID of the current cursor.
     *
     * @return the <code>CUBRIDOID</code> object of the current cursor if OID is included, <code>
     *     null</code> otherwise.
     * @exception SQLException if <code>this</code> object is closed.
     * @exception SQLException if the current cursor is on beforeFirst, afterLast or the insert row
     * @exception SQLException if a database access error occurs
     */
    public synchronized CUBRIDOID getOID() throws SQLException {
        // TODO
        CUBRIDOID value = null;
        return value;
    }

    /* JDK 1.6 */
    public int getHoldability() throws SQLException {
        return (isHoldable
                ? ResultSet.HOLD_CURSORS_OVER_COMMIT
                : ResultSet.CLOSE_CURSORS_AT_COMMIT);
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
        // TODO
        return true;
    }

    /* JDK 1.6 */
    public void updateAsciiStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(int columnIndex, InputStream x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateAsciiStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(int columnIndex, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(String columnLabel, InputStream x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(int columnIndex, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBinaryStream(String columnLabel, InputStream x, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(int columnIndex, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(String columnLabel, InputStream inputStream) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(int columnIndex, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateBlob(String columnLabel, InputStream inputStream, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(int columnIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(int columnIndex, Reader x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(int columnIndex, Reader x, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNCharacterStream(String columnLabel, Reader reader, long length)
            throws SQLException {
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
    public void updateNClob(String columnLabel, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(int columnIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNClob(String columnLabel, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNString(int columnIndex, String string) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateNString(String columnLabel, String string) throws SQLException {
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
    public void updateSQLXML(int columnIndex, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void updateSQLXML(String columnLabel, SQLXML xmlObject) throws SQLException {
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
    public <T> T getObject(String columnLabel, Class<T> type) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
