package com.cubrid.jsp.task;

import com.cubrid.jsp.communication.ConnectionEntry;
import com.cubrid.jsp.handler.DispatchHandler;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.task.SystemTask;
import com.cubrid.jsp.task.Task;
import java.nio.channels.SelectionKey;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;


public class ExecutorManager {
    // singleton
    private static ExecutorManager instance = null;

    public static ExecutorManager getExecutorManager () {
        if (instance == null) {
            instance = new ExecutorManager ();
        }
        return instance;
    }

    private static ExecutorService connectionPool = null; // To handle ping, stopping server
    private static ExecutorService workerPool = null; // To handle execution

    private ExecutorManager () {
        // single thread to handle ping, status, shutting down
        connectionPool = Executors.newSingleThreadExecutor();
        
        // adaptive scalable worker pool
        workerPool = new ThreadPoolExecutor(4, Integer.MAX_VALUE, 60L, TimeUnit.SECONDS, new SynchronousQueue<Runnable>());
    }

    public static void submit (SelectionKey key) {
        workerPool.submit(new DispatchHandler (key));
    }
}
