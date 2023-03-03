package org.cubrid.pl;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

public class ContextManager {

    // singleton
    private static ContextManager instance = null;
    
    public static ContextManager getContextManager () {
        if (instance == null) {
            instance = new ContextManager ();
        }
        return instance;
    }

    // Context ID => Context Object
    private ConcurrentMap<Long, Context> contextMap = new ConcurrentHashMap<Long, Context> ();

    public boolean hasContext (long id) {
        return contextMap.containsKey(id);
    }

    public Context getContext (long id) {
        if (hasContext (id)) {
            return contextMap.get (id);
        } else {
            Context newCtx = new Context (id);
            contextMap.put (id, newCtx);
            return newCtx;
        }
    }

    // Java Thread ID => Context ID
    private ConcurrentMap<Long, Long> contextThreadMap = new ConcurrentHashMap<Long, Long> ();

    public Long getContextByThreadId (long id) {
        if (contextThreadMap.containsKey(id)) {
            return contextThreadMap.get (id);
        }

        // TODO: exception? should have not been occured
        return null;
    }
}
