package com.cubrid.jsp.task;

import java.nio.channels.SelectionKey;

import com.cubrid.jsp.protocol.Header;

public class TaskFactory {

    public static Task getTask (Header header, SelectionKey key) {
        if (header == null) {
            // return new ConnectionTask(connEntry);
        } else if (header.id == Header.EMPTY_SESSION_ID) {
            // return new SystemTask(header, key);
        } else {
            // return new ExecutionTask(connEntry, header);
        }
        return null;
    }

}
