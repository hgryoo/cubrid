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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.Statement;

public class CUBRIDServerSideStatement implements Statement {

    CUBRIDServerSideConnection con;

    protected CUBRIDServerSideStatement() {}

    public ResultSet executeQuery(String sql) throws SQLException {
        // TODO
        return null;
    }

    public int executeUpdate(String sql) throws SQLException {
        return executeUpdate(sql, Statement.NO_GENERATED_KEYS);
    }

    public void close() throws SQLException {
        // TODO
    }

    public synchronized int getMaxFieldSize() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized void setMaxFieldSize(int max) throws SQLException {
        if (max < 0) {
            throw new IllegalArgumentException();
        }
        // TODO
    }

    public synchronized int getMaxRows() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized void setMaxRows(int max) throws SQLException {
        if (max < 0) {
            throw new IllegalArgumentException();
        }
        // TODO
    }

    public synchronized void setEscapeProcessing(boolean enable) throws SQLException {
        /* do nothing */
    }

    public synchronized int getQueryTimeout() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized void setQueryTimeout(int seconds) throws SQLException {
        // TODO
    }

    public void cancel() throws SQLException {
        // TODO
    }

    public synchronized SQLWarning getWarnings() throws SQLException {
        /* do nothing */
        return null;
    }

    public synchronized void clearWarnings() throws SQLException {
        /* do nothing */
    }

    public synchronized void setCursorName(String name) throws SQLException {
        /* do nothing */
    }

    public boolean execute(String sql) throws SQLException {
        return execute(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized ResultSet getResultSet() throws SQLException {
        // return current_result_set;
        // TODO
        return null;
    }

    public synchronized int getUpdateCount() throws SQLException {
        // TODO
        return 0;
    }

    public boolean getMoreResults() throws SQLException {
        // TODO
        return true;
    }

    public synchronized void setFetchDirection(int direction) throws SQLException {
        // TODO
        /*
        if (!is_scrollable)
            throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_scrollable_statement, null);

        switch (direction) {
            case ResultSet.FETCH_FORWARD:
            case ResultSet.FETCH_REVERSE:
            case ResultSet.FETCH_UNKNOWN:
                fetch_direction = direction;
                break;
            default:
                throw new IllegalArgumentException();
        }
        */
    }

    public synchronized int getFetchDirection() throws SQLException {
        // TODO
        // return fetch_direction;
        return 1;
    }

    public synchronized void setFetchSize(int rows) throws SQLException {
        // TODO
        if (rows < 0) {
            throw new IllegalArgumentException();
        }
        // fetch_size = rows;
    }

    public synchronized int getFetchSize() throws SQLException {
        // return fetch_size;
        // TODO
        return 0;
    }

    public synchronized int getResultSetConcurrency() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized int getResultSetType() throws SQLException {
        // TODO
        return 0;
    }

    public synchronized void addBatch(String sql) throws SQLException {
        // TODO
    }

    public synchronized void clearBatch() throws SQLException {
        // TODO
    }

    public int[] executeBatch() throws SQLException {
        // TODO
        return null;
    }

    public Connection getConnection() throws SQLException {
        return con;
    }

    public boolean getMoreResults(int current) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public synchronized ResultSet getGeneratedKeys() throws SQLException {
        // TODO
        return null;
    }

    public synchronized int executeUpdate(String sql, int autoGeneratedKeys) throws SQLException {
        // TODO
        return 0;
    }

    public synchronized int executeUpdate(String sql, int[] columnIndexes) throws SQLException {
        return executeUpdate(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized int executeUpdate(String sql, String[] columnNames) throws SQLException {
        return executeUpdate(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized boolean execute(String sql, int autoGeneratedKeys) throws SQLException {
        return execute(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized boolean execute(String sql, int[] columnIndexes) throws SQLException {
        return execute(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized boolean execute(String sql, String[] columnNames) throws SQLException {
        return execute(sql, Statement.NO_GENERATED_KEYS);
    }

    public synchronized int getResultSetHoldability() throws SQLException {
        // TODO
        return 0;
        // return getHoldability();
    }

    /* JDK 1.6 */
    public boolean isClosed() throws SQLException {
        // TODO
        return false;
    }

    /* JDK 1.6 */
    public boolean isPoolable() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void setPoolable(boolean poolable) throws SQLException {
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
    public void closeOnCompletion() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.7 */
    public boolean isCloseOnCompletion() throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }
}
