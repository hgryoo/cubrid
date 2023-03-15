package com.cubrid.jsp.context;

import java.nio.ByteBuffer;
import java.util.concurrent.LinkedBlockingQueue;

import com.cubrid.jsp.communication.ConnectionEntry;
import com.cubrid.jsp.value.Value;

public class ProcedureStack {
    public static final long DEFAULT_PROCEDURE_GROUP_ID = -1L;

    private long id = DEFAULT_PROCEDURE_GROUP_ID;
    private Value[] args = null;
    private int depth = 0;

    private ConnectionEntry connEntry = null;

    public ProcedureStack (long id, int depth) {
        this.id = id;
        this.depth = depth;
    }

    public long getId () {
        return id;
    }

    public void setArguments (Value[] args) {
        this.args = args;
    }

    public Value[] getArguments () {
        return args;
    }

    public void setConnectionEntry (ConnectionEntry entry) {
        this.connEntry = entry;
    }

    public ConnectionEntry getConnectionEntry () {
        return connEntry;
    }
    
    public int getDepth () {
        return depth;
    }
}
