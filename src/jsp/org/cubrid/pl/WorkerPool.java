package org.cubrid.pl;

import java.util.Queue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class WorkerPool {

    // singleton
    private static WorkerPool instance = null;
    
    private BlockingQueue<Task> taskQueue = null;
    private BlockingQueue<Runnable> workQueue = null;

    public static WorkerPool getWrokerPool () {
        if (instance == null) {
            instance = new WorkerPool();
        }
        return instance;
    }
        
    private AtomicInteger workerCount = new AtomicInteger (0);
    private AtomicInteger activeWorkerCount = new AtomicInteger (0);
    private ThreadPoolExecutor executor;
    
    private WorkerPool ()
    {
        int numOfCores = Runtime.getRuntime().availableProcessors();

        taskQueue = new LinkedBlockingQueue<Task> ();
        executor = new ThreadPoolExecutor (
            16,
            1024,
            600,
            TimeUnit.MILLISECONDS,
            workQueue
        );
    }

    public void execute (Task task) {
        if (task == null) {
            throw new NullPointerException("task is null");
        }

        taskQueue.add(task);
    }

    public int getWorkerCount () 
    {
        return workerCount.get();
    }

    public void incrementWorkerCount ()
    {
        workerCount.incrementAndGet();
    }

    public void decrementWorkerCount ()
    {
        workerCount.decrementAndGet();
    }
}
