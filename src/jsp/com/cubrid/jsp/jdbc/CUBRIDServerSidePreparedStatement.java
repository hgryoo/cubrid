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
import cubrid.sql.CUBRIDTimestamptz;
import java.io.InputStream;
import java.io.Reader;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.ParameterMetaData;
import java.sql.PreparedStatement;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLXML;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSidePreparedStatement extends CUBRIDServerSideStatement
        implements PreparedStatement {

    protected CUBRIDServerSidePreparedStatement() {}

    /*
     * java.sql.PreparedStatement interface
     */
    public ResultSet executeQuery() throws SQLException {
        // TODO
        return null;
    }

    public int executeUpdate() throws SQLException {
        // TODO
        return 1;
    }

    public void setNull(int parameterIndex, int sqlType) throws SQLException {
        // TODO
    }

    public void setBoolean(int parameterIndex, boolean x) throws SQLException {
        // TODO
    }

    public void setByte(int parameterIndex, byte x) throws SQLException {
        // TODO
    }

    public void setShort(int parameterIndex, short x) throws SQLException {
        // TODO
    }

    public void setInt(int parameterIndex, int x) throws SQLException {
        // TODO
    }

    public void setLong(int parameterIndex, long x) throws SQLException {
        // TODO
    }

    public void setFloat(int parameterIndex, float x) throws SQLException {
        // TODO
    }

    public void setDouble(int parameterIndex, double x) throws SQLException {
        // TODO
    }

    public void setBigDecimal(int parameterIndex, BigDecimal x) throws SQLException {
        // TODO
    }

    public void setString(int parameterIndex, String x) throws SQLException {
        // TODO
    }

    public void setBytes(int parameterIndex, byte[] x) throws SQLException {
        // TODO
    }

    public void setDate(int parameterIndex, Date x) throws SQLException {
        // TODO
    }

    public void setTime(int parameterIndex, Time x) throws SQLException {
        // TODO
    }

    public void setTimestamp(int parameterIndex, Timestamp x) throws SQLException {
        // TODO
    }

    public void setTimestamptz(int parameterIndex, CUBRIDTimestamptz x) throws SQLException {
        // TODO
    }

    public void setAsciiStream(int parameterIndex, InputStream x, int length) throws SQLException {
        // TODO
    }

    public void setUnicodeStream(int parameterIndex, InputStream x, int length)
            throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBinaryStream(int parameterIndex, InputStream x, int length) throws SQLException {
        // TODO
    }

    public void clearParameters() throws SQLException {
        // TODO
    }

    public void setObject(int parameterIndex, Object x, int targetSqlType, int scale)
            throws SQLException {
        // TODO
    }

    public void setObject(int parameterIndex, Object x, int targetSqlType) throws SQLException {
        // TODO
    }

    public void setObject(int parameterIndex, Object x) throws SQLException {
        // TODO
    }

    public boolean execute() throws SQLException {
        // TODO
        return true;
    }

    public void addBatch() throws SQLException {
        // TODO
    }

    public void setCharacterStream(int parameterIndex, Reader reader, int length)
            throws SQLException {
        // TODO
    }

    public void setRef(int i, Ref x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setBlob(int parameterIndex, Blob x) throws SQLException {
        // TODO
    }

    public void setClob(int parameterIndex, Clob x) throws SQLException {
        // TODO
    }

    /* JDK 1.6 */
    public void setBlob(int parameterIndex, InputStream inputStream) throws SQLException {
        // TODO
    }

    /* JDK 1.6 */
    public void setBlob(int parameterIndex, InputStream inputStream, long length)
            throws SQLException {
        // TODO
    }

    /* JDK 1.6 */
    public void setClob(int parameterIndex, Reader reader) throws SQLException {
        // TODO
    }

    /* JDK 1.6 */
    public void setClob(int parameterIndex, Reader reader, long length) throws SQLException {
        // TODO
    }

    public void setArray(int i, Array x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public ResultSetMetaData getMetaData() throws SQLException {
        // TODO
        return null;
    }

    public void setDate(int parameterIndex, Date x, Calendar cal) throws SQLException {
        setDate(parameterIndex, x);
    }

    public void setTime(int parameterIndex, Time x, Calendar cal) throws SQLException {
        setTime(parameterIndex, x);
    }

    public void setTimestamp(int parameterIndex, Timestamp x, Calendar cal) throws SQLException {
        setTimestamp(parameterIndex, x);
    }

    public void setTimestamptz(int parameterIndex, CUBRIDTimestamptz x, Calendar cal)
            throws SQLException {
        setTimestamptz(parameterIndex, x);
    }

    public void setNull(int paramIndex, int sqlType, String typeName) throws SQLException {
        setNull(paramIndex, sqlType);
    }

    public void close() throws SQLException {
        // TODO
    }

    public void clearBatch() throws SQLException {
        // TODO
    }

    public int[] executeBatch() throws SQLException {
        // TODO
        return null;
    }

    // 3.0
    public ParameterMetaData getParameterMetaData() throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setURL(int index, URL x) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    // 3.0

    public synchronized void setOID(int parameterIndex, CUBRIDOID x) throws SQLException {
        // TODO
    }

    /**
     * Sets the designated parameter to a Java <code>Object[]</code> value. The driver converts this
     * to an <code>CUBRID COLLECTION</code> value when it sends it to the database.
     *
     * @param parameterIndex the first parameter is 1, the second is 2, ...
     * @param x the parameter
     * @exception SQLException if <code>this</code> object is closed.
     * @exception IllegalArgumentException if <code>x</code> is not <code>null</code> and all
     *     attempts to cast <code>x</code> to one of the following types fail.<br>
     *     <code>Boolean[], Integer[], Short[], Byte[], Float[], Double[],
     * Date[], Time[], Timestamp[], String[], byte[][], CUBRIDOID[]</code>
     * @exception SQLException if <code>parameterIndex</code> is out of range.
     * @exception SQLException if a database access error occurs
     */
    public synchronized void setCollection(int parameterIndex, Object[] x) throws SQLException {
        // TODO
    }

    /**
     * Executes an SQL <code>INSERT</code> statement in <code>this PreparedStatement</code> object
     * and returns a <code>CUBRIDOID</code> object that represents the OID of the object inserted by
     * the statement.
     *
     * @return a <code>CUBRIDOID</code> object that represents the OID of the object inserted by the
     *     statement.
     * @exception SQLException if <code>this</code> object is closed.
     * @exception SQLException if the statement in <code>this PreparedStatement</code> object is not
     *     an SQL <code>INSERT</code> statement.
     * @exception SQLException if a database access error occurs
     */
    public CUBRIDOID executeInsert() throws SQLException {
        return null;
    }

    public boolean hasResultSet() {
        // TODO
        return true;
    }

    /* JDK 1.6 */
    public void setBinaryStream(int parameterIndex, InputStream x) throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setBlob(parameterIndex, x);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setBinaryStream(int parameterIndex, InputStream x, long length)
            throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setBlob(parameterIndex, x, length);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setAsciiStream(int parameterIndex, InputStream x) throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setClob(parameterIndex, x);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setAsciiStream(int parameterIndex, InputStream x, long length) throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setClob(parameterIndex, x, length);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setCharacterStream(int parameterIndex, Reader reader) throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setClob(parameterIndex, reader);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setCharacterStream(int parameterIndex, Reader reader, long length)
            throws SQLException {
        // TODO: How to solve it? host variable bind problem
        // setClob(parameterIndex, reader, length);
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNCharacterStream(int parameterIndex, Reader value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNCharacterStream(int parameterIndex, Reader value, long length)
            throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(int parameterIndex, NClob value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(int parameterIndex, Reader reader) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNClob(int parameterIndex, Reader reader, long length) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setNString(int parameterIndex, String value) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setRowId(int parameterIndex, RowId x) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setSQLXML(int parameterIndex, SQLXML xmlObject) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
