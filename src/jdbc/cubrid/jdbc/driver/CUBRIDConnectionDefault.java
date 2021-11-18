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

package cubrid.jdbc.driver;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UServerSideConnection;
import cubrid.jdbc.jci.UStatement;
import cubrid.jdbc.jci.UUserInfo;
import java.sql.CallableStatement;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Properties;

/**
 * Title: CUBRID JDBC Driver Description:
 *
 * @version 2.0
 */
public class CUBRIDConnectionDefault extends CUBRIDConnection {

	public CUBRIDConnectionDefault(UConnection u, String r, String s) {
		super(u, r, s);
		this.auto_commit = false;
	}

	@Override
	public void setAutoCommit(boolean autoCommit) {
		/* do nothing */
	}

	@Override
	public void commit() throws SQLException {
		/* do nothing */
	}

	@Override
	public void rollback() throws SQLException {
		/* do nothing */
	}

	@Override
	protected void autoCommit() {
		/* do nothing */
	}

	@Override
	protected void autoRollback() {
		/* do nothing */
	}

	protected UServerSideConnection getJciConnection() {
		return (UServerSideConnection) u_con;
	}

	@Override
	public synchronized CallableStatement prepareCall(String sql) throws SQLException {
		UStatement us = prepare(sql, UConnection.PREPARE_CALL);
		CallableStatement cstmt = new CUBRIDCallableStatement(this, us);
		addStatement(cstmt);

		return cstmt;
	}

	@Override
	protected PreparedStatement prepare(String sql, int resultSetType, int resultSetConcurrency, int resultHoldability,
			int autoGeneratedKeys) throws SQLException {
		byte prepareFlag = (byte) 0;

		if (resultSetType == ResultSet.TYPE_SCROLL_SENSITIVE || resultSetConcurrency == ResultSet.CONCUR_UPDATABLE) {
			prepareFlag = UConnection.PREPARE_UPDATABLE;
		}
		if (resultHoldability == ResultSet.HOLD_CURSORS_OVER_COMMIT && u_con.supportHoldableResult()) {
			prepareFlag = UConnection.PREPARE_HOLDABLE;
		}

		UStatement us = prepare(sql, prepareFlag);
		PreparedStatement pstmt = new CUBRIDServerSidePreparedStatement(this, us, resultSetType, resultSetConcurrency,
				resultHoldability, autoGeneratedKeys);
		addStatement(pstmt);

		return pstmt;
	}

	@Override
	public synchronized void close() throws SQLException {
		if (is_closed)
			return;

		/* assuming that u_con is UConnectionServer */
		UServerSideConnection uServerConnection = (UServerSideConnection) u_con;

		/* send UFunctionCode.CON_CLOSE without clearing Connection's resources */
		uServerConnection.close();

		/* clear JCI wrappers */
		outRs.clear();
		statements.clear();
	}

	@Override
	protected void finalize() {
		super.finalize();
		/* do nothing */
	}

	public synchronized void destroy() throws SQLException {
		/* assuming that u_con is UConnectionServer */
		UServerSideConnection uServerConnection = (UServerSideConnection) u_con;
		uServerConnection.destroy();
		clear();
		uServerConnection.close();
		u_con = null;
		url = null;
		user = null;
		mdata = null;
		statements = null;
		error = null;
		shard_mdata = null;
		is_closed = true;
	}

	/* JDK 1.6 */
	@Override
	public Properties getClientInfo() throws SQLException {
		UUserInfo info = getJciConnection().getUserInfo();

		Properties props = new Properties();
		if (info != null) {
			props = info.toProperties();
		}
		return props;
	}
}
