package org.cubrid.pl;

import java.util.concurrent.atomic.AtomicBoolean;

public class Worker {
    private WorkerPool pool;
    private AtomicBoolean running = new AtomicBoolean (true);
    private WorkerState state;

    private Worker (WorkerPool pool) {
        this.pool = pool;
    }

    public void run() {
        initialize ();
        try {
            while (isRunning ()) {

                Task task = getNewTask();


                long begin = System.currentTimeMillis();

                
            }
        } catch (Exception e) {

        } finally {

        }
    }

    public Task getNewTask () {
        // TODO
        return null;
    }

    public void initialize () {
        setState(WorkerState.STARTED);

    }

    public void shutdown () {
        setState(WorkerState.STOPPED);
    }

    public boolean isRunning () {
        return running.get ();
    }

    private void setState (WorkerState state) {
        this.state = state;
    }
}
