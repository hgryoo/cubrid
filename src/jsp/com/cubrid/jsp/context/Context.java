package com.cubrid.jsp.context;

import com.cubrid.jsp.StoredProcedure;
import com.cubrid.jsp.communication.ConnectionEntry;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.task.Request;
import com.cubrid.jsp.value.Value;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.LinkedBlockingQueue;

public class Context {
    // To recognize unique DB session
    private long sessionId = -1;

    // If transaction ID is changed, The server-side connection must be reset
    private long transactionId = -1;

    // charset 
    private String charSet = "UTF-8";

    // single server-side connection per Context
    private CUBRIDServerSideConnection connection = null;    
    private LinkedBlockingQueue<ByteBuffer> inbound = null;

    // CAS client information connecting with this Context
    private Properties clientInfo = null;

    private Map<Long, ProcedureStack> procGroupMap = new HashMap <Long, ProcedureStack> ();

    // Stack depth currently running
    private int stackIdx = -1;

    // A procedure group currently running
    private ProcedureStack runningGroup = null;

    // TODO: dynamic classLoader

    // Stored Procedure Objects
    private ArrayList <StoredProcedure> procList = new ArrayList<>();
    private Map<String, Integer> procObjectMap = new HashMap <String, Integer> ();

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

    public synchronized Connection getConnection() {
        if (this.connection == null) {
            this.connection = new CUBRIDServerSideConnection(this);
        }
        return connection;
    }

    public void closeConnection (Connection conn) throws SQLException {
        if (connection != null) {
            connection.close();
        }
    }

    public Properties getClientInfo () {
        if (clientInfo == null) {
            clientInfo = new Properties ();
        }
        return clientInfo;
    }

    public Integer getProcedureIndex (String signature) {
        if (procObjectMap.containsKey(signature)) {
            return procObjectMap.get(signature);
        }
        return -1;
    }

    public StoredProcedure getProcedure (String signature) throws Exception {
        if (procObjectMap.containsKey(signature)) {
            int idx = procObjectMap.get (signature);
            return procList.get(idx);
        } else {
            StoredProcedure proc = new StoredProcedure (signature);
            procList.add (proc);
            procObjectMap.put(signature, procList.size () - 1);
            return proc;
        }
    }

    public ProcedureStack getProcedureStack (long id) {
        ProcedureStack group = null;
        if (procGroupMap.containsKey(id)) {
            group = procGroupMap.get(id);
            stackIdx = group.getDepth();
        } else {
            stackIdx++;
            group = new ProcedureStack (id, stackIdx);
            procGroupMap.put(id, group);
        }
        runningGroup = group;
        return group;
    }

    public void setRunningStack (long id) {
        runningGroup =  procGroupMap.get(id);
    }

    public ProcedureStack getRunningStack () {
        return runningGroup;
    }

    public LinkedBlockingQueue<ByteBuffer> getInboundQueue () {
        if (inbound == null) {
            inbound = new LinkedBlockingQueue<ByteBuffer> ();
        }
        return inbound;
    }

    public String getCharset () {
        return charSet;
    }
}
