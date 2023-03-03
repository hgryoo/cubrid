package org.cubrid.pl;

import com.cubrid.jsp.StoredProcedure;
import java.sql.Connection;
import java.util.Stack;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;
    private long sessionActiveTime;

    // If transaction ID is changed, the server-side connection must be reset
    private long transactionId = -1;

    // single server-side connection per Context
    private Connection connection = null;

    private Stack<StoredProcedure> procStack = null;

    public Context (long id) {
        sessionId = id;
    }

    public long getSessionId() {
        return sessionId;
    }

    public long getSessionActiveTime() {
        return sessionActiveTime;
    }

    public long getTransactionId() {
        return transactionId;
    }

    public void setTransactionId(long transactionId) {
        this.transactionId = transactionId;
    }

    public Connection getConnection() {
        return connection;
    }

    public void setConnection(Connection connection) {
        this.connection = connection;
    }
}
