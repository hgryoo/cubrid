package com.cubrid.jsp.task;

import com.cubrid.jsp.communication.ConnectionEntry;

public abstract class Task implements Runnable {
    private ConnectionEntry connEntry = null;
    
    public Task () {
        
    }
    
    public Task (ConnectionEntry entry) {
        connEntry = entry;
    }

    public ConnectionEntry getConnectionEntry () {
        return connEntry;
    }

    public boolean hasOutput () {
        return false;
    }

    public boolean hasPayload () {
        return false;
    }
}
