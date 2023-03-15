package com.cubrid.jsp.handler;
import com.cubrid.jsp.Server;
import com.cubrid.jsp.communication.ConnectionEntryPool;
import com.cubrid.jsp.task.ExecutorManager;
import java.io.IOException;
import java.nio.channels.SelectableChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.Iterator;

public class AcceptHandler implements Runnable {

    private Selector selector;
    private ServerSocketChannel serverChannel;

    public AcceptHandler (Selector selector, ServerSocketChannel serverChannel) {
        this.selector = selector;
        this.serverChannel = serverChannel;
    }

    @Override
    public void run() {
        try {
            registerChannel (selector, serverChannel, SelectionKey.OP_ACCEPT);
            while (true) {
                int n = selector.select();
                if (n == 0) {
                    continue; // nothing to do
                }

                Iterator<SelectionKey> it = selector.selectedKeys().iterator();
                while (it.hasNext()) {
                  SelectionKey key = it.next();
                  it.remove();
                  if (key.isAcceptable()) {
                    accept (key);
                  } else if (key.isReadable()) {
                    read (key);
                  }
                }
            }
        } catch (Exception e) {
            Server.log (e);
        }
    }

    private void accept (SelectionKey key) throws IOException {
      ServerSocketChannel readyChannel = (ServerSocketChannel) key.channel();
      SocketChannel channel = readyChannel.accept();
      registerChannel(selector, channel, SelectionKey.OP_READ);
    }

    private void read (SelectionKey key) {
      key.interestOps(SelectionKey.OP_WRITE);
      ConnectionEntryPool.getConnectionEntryPool().create (key);
      ExecutorManager.submit (key);
    }

    private void registerChannel (Selector selector, SelectableChannel channel, int ops) throws IOException {
        channel.configureBlocking(false);
        channel.register(selector, ops);
    }
}
