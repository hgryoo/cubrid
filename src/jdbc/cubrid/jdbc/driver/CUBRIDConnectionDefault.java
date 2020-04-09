package cubrid.jdbc.driver;

import java.sql.SQLException;

import cubrid.jdbc.jci.UConnection;

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
	public void commit() {
		/* do nothing */
	}

	@Override
	public void rollback() {
		/* do nothing */
	}

	/* Server-side connection doesn't clear JDBC resources */
	@Override
	public synchronized void close() throws SQLException {
		if (is_closed)
			return;

		u_con.close();
		is_closed = true;
	}
}