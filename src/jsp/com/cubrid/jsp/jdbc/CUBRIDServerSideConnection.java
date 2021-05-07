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

import java.sql.Array;
import java.sql.Blob;
import java.sql.CallableStatement;
import java.sql.Clob;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.NClob;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLClientInfoException;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Savepoint;
import java.sql.Statement;
import java.sql.Struct;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.Executor;

import java.util.concurrent.CopyOnWriteArrayList;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDServerSideConnection implements Connection {

    int holdability;

    protected CUBRIDServerSideDatabaseMetaData mdata = null;
    protected CopyOnWriteArrayList<Statement> statements = null;

    public CUBRIDServerSideConnection() {
        // there is no meaning for the holdable cursor on server-side
        holdability = ResultSet.HOLD_CURSORS_OVER_COMMIT;
        statements = new CopyOnWriteArrayList<Statement> ();
    }

    public Statement createStatement() throws SQLException {
        return createStatement(ResultSet.TYPE_FORWARD_ONLY, ResultSet.CONCUR_READ_ONLY);
    }

    public PreparedStatement prepareStatement(String sql) throws SQLException {
        // TODO
        return null;
    }

    public CallableStatement prepareCall(String sql) throws SQLException {
        // TODO
        return null;
    }

    public String nativeSQL(String sql) throws SQLException {
        throw new SQLException(new UnsupportedOperationException());
    }

    public void setAutoCommit(boolean autoCommit) {
        /* do nothing */
    }

    public boolean getAutoCommit() throws SQLException {
        /* always false */
        return false;
    }

    public void commit() throws SQLException {
        /* do nothing */
    }

    public void rollback() throws SQLException {
        /* do nothing */
    }

    public synchronized void close() throws SQLException {
        /* Becuase JDBC code is running inside the single Java SP server, It should not be closed */
        /* The connection is an implicit data channel, not an explicit connection instance as from a client. */
        /* do nothing */
    }

    public boolean isClosed() throws SQLException {
        /* always false */
        return false;
    }

    public DatabaseMetaData getMetaData() throws SQLException {
        if (mdata == null) {
            mdata = new CUBRIDServerSideDatabaseMetaData(this);
        }
        return mdata;
    }

    public void setReadOnly(boolean arg0) throws SQLException {
        /* do nothing */
    }

    public boolean isReadOnly() throws SQLException {
        /* do nothing */
        return false;
    }

    public void setCatalog(String catalog) throws SQLException {
        /* do nothing */
    }

    public String getCatalog() throws SQLException {
        /* do nothing */
        return "";
    }

    public void setTransactionIsolation(int level) throws SQLException {
        /* do nothing */
        /* transaction isolation should not be set by server-side connection */
    }

    public int getTransactionIsolation() throws SQLException {
        // TODO
        // getTransactionIsolation from Server
        return 0;
    }

    public SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    public void clearWarnings() throws SQLException {
        /* do nothing */
    }

    public Statement createStatement(int resultSetType, int resultSetConcurrency)
            throws SQLException {
        Statement stmt = new CUBRIDServerSideStatement(this, resultSetType, resultSetConcurrency, holdability);
        statements.add (stmt);
        return stmt;
    }

    public PreparedStatement prepareStatement(
            String sql, int resultSetType, int resultSetConcurrency) throws SQLException {
        // TODO
        return null;
    }

    public CallableStatement prepareCall(String sql, int resultSetType, int resultSetConcurrency)
            throws SQLException {
        // TODO
        return null;
    }

    public Map<String, Class<?>> getTypeMap() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void setTypeMap(Map<String, Class<?>> map) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized void setHoldability(int holdable) throws SQLException {
        /* do nothing */
    }

    public synchronized int getHoldability() throws SQLException {
        /* do nothing, return default value */
        return holdability;
    }

    public Savepoint setSavepoint() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public Savepoint setSavepoint(String name) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void rollback(Savepoint savepoint) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public void releaseSavepoint(Savepoint savepoint) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized Statement createStatement(int resultSetType, int resultSetConcurrency, int holdable) {
        Statement stmt = new CUBRIDServerSideStatement(this, resultSetType, resultSetConcurrency, holdable);
        statements.add (stmt);
        return stmt;
    }

    public synchronized PreparedStatement prepareStatement(
            String sql, int type, int concur, int holdable) throws SQLException {
        // TODO
        if (holdable == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
            if (type == ResultSet.TYPE_SCROLL_SENSITIVE || concur == ResultSet.CONCUR_UPDATABLE) {
                throw new SQLException(new java.lang.UnsupportedOperationException());
            }
        }
        return null;
        // return prepare(sql, type, concur, holdable, Statement.NO_GENERATED_KEYS);
    }

    public CallableStatement prepareCall(String sql, int type, int concur, int holdable)
            throws SQLException {
        return prepareCall(sql);
    }

    public synchronized PreparedStatement prepareStatement(String sql, int autoGeneratedKeys)
            throws SQLException {
        /*
        return prepare(
                sql,
                ResultSet.TYPE_FORWARD_ONLY,
                ResultSet.CONCUR_READ_ONLY,
                holdability,
                autoGeneratedKeys);
                */
        return null;
    }

    public synchronized PreparedStatement prepareStatement(String sql, int[] indexes)
            throws SQLException {
        return prepareStatement(sql);
    }

    public synchronized PreparedStatement prepareStatement(String sql, String[] colName)
            throws SQLException {
        return prepareStatement(sql);
    }

    /* JDK 1.6 */
    public Clob createClob() throws SQLException {
        // TODO
        // Clob clob = new CUBRIDClob(this, getUConnection().getCharset());
        return null;
    }

    /* JDK 1.6 */
    public Blob createBlob() throws SQLException {
        // TODO
        // Blob blob = new CUBRIDBlob(this);
        return null;
    }

    /* JDK 1.6 */
    public NClob createNClob() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public SQLXML createSQLXML() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public synchronized boolean isValid(int timeout) throws SQLException {
        if (timeout < 0) {
            throw new SQLException();
        }

        // TODO
        // if (u_con == null || is_closed) return false;
        // return u_con.isValid(timeout * 1000);
        return true;
    }

    /* JDK 1.6 */
    public void setClientInfo(Properties arg0) throws SQLClientInfoException {
        SQLClientInfoException clientEx = new SQLClientInfoException();
        clientEx.initCause(new java.lang.UnsupportedOperationException());
        throw clientEx;
    }

    /* JDK 1.6 */
    public void setClientInfo(String arg0, String arg1) throws SQLClientInfoException {
        SQLClientInfoException clientEx = new SQLClientInfoException();
        clientEx.initCause(new java.lang.UnsupportedOperationException());
        throw clientEx;
    }

    /* JDK 1.6 */
    public Properties getClientInfo() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public String getClientInfo(String arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Array createArrayOf(String arg0, Object[] arg1) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public Struct createStruct(String arg0, Object[] arg1) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void setSchema(String schema) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public String getSchema() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void abort(Executor executor) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public void setNetworkTimeout(Executor executor, int milliseconds) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public int getNetworkTimeout() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    // From java.sql.Wrapper
    /* JDK 1.6 */
    public boolean isWrapperFor(Class<?> arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public <T> T unwrap(Class<T> arg0) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
