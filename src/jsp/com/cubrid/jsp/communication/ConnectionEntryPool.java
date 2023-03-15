package com.cubrid.jsp.communication;

import java.nio.channels.SelectionKey;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicInteger;

public class ConnectionEntryPool {

    private BlockingQueue <ConnectionEntry> available = new LinkedBlockingQueue <ConnectionEntry>();
    private AtomicInteger InUseCnt = new AtomicInteger(0);

    // singleton
    private ConnectionEntryPool () {
        //
    }

    private static ConnectionEntryPool instance = null;
    public static ConnectionEntryPool getConnectionEntryPool () {
        if (instance == null) {
            instance = new ConnectionEntryPool ();
        }
        return instance;
    }

    public void create (SelectionKey key) {
        ConnectionEntry entry = new ConnectionEntry (key);
        key.attach (entry);
    }

    /*
    public ConnectionEntry claim () {
        ConnectionEntry obj = null;
        try {
            obj = available.take();
            InUseCnt.incrementAndGet();
            return obj;
        } catch (InterruptedException e) {
            // do nothing
        }
        return obj;
    }

    public void retire (ConnectionEntry obj) {
        available.offer (obj);
        InUseCnt.decrementAndGet();
    }
    */
}
