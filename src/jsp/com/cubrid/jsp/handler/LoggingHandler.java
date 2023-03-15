package com.cubrid.jsp.handler;

import java.io.IOException;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.logging.FileHandler;
import java.util.logging.Level;
import java.util.logging.Logger;

public class LoggingHandler implements Runnable {
    private static final Logger logger = Logger.getLogger("com.cubrid.jsp");
    private static final String LOG_DIR = "log";

    private FileHandler logHandler = null;
    private LinkedBlockingQueue<String> logQueue = new LinkedBlockingQueue<String> ();
    private Level logginLevel = Level.SEVERE;
    
    @Override
    public void run() {
        while (Thread.interrupted() == false) {
            try {
                String logString = logQueue.take();
                logger.log(logginLevel, logString);
            } catch (InterruptedException e) {
                break;
            }
        }
        
        if (logHandler != null) {
            try {
                logHandler.close();
                logger.removeHandler(logHandler);
            } catch (Throwable e) {
            }
        }
    }

    public LoggingHandler (String path) throws SecurityException, IOException {
        logHandler = new FileHandler(path, true);
        logger.addHandler(logHandler);
    }

    public void putLog (String log) {
        try {
            logQueue.put(log);
        } catch (InterruptedException e) {
        }
    }
}
