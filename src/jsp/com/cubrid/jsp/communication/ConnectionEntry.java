package com.cubrid.jsp.communication;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.protocol.Header;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;

public class ConnectionEntry {
    public static final int DEFAULT_PACKER_SIZE = 1024;

    private SelectionKey key;
    private SocketChannel channel;

    /* TODO: It will be replaced with DirectByteBuffer-based new buffer which dynamically extended if overflow exists */
    /* Since DirectByteBuffer's allocation time is slow, DirectByteBuffer pooling should be implemented */
    private ByteBuffer inBuffer = null;

    private ByteBuffer inSizeBuffer = ByteBuffer.allocateDirect(Integer.BYTES);
    private ByteBuffer outSizeBuffer = ByteBuffer.allocateDirect(Integer.BYTES);

    private ByteBuffer headerBuffer = ByteBuffer.allocate(Header.BYTES);

    public ConnectionEntry (SelectionKey key) {
        this.key = key;
        key.attach(this);

        channel = (SocketChannel) key.channel();
    }

    public int readSize () throws IOException {
        inSizeBuffer.clear ();

        while (inSizeBuffer.position() != inSizeBuffer.capacity()) {
           channel.read (inSizeBuffer);
        }
        inSizeBuffer.flip ();
        return inSizeBuffer.getInt();
    }

    public void writeSize (int size) throws IOException {
        outSizeBuffer.clear ();
        outSizeBuffer.putInt(size);
        outSizeBuffer.flip(); // write mode
        while (outSizeBuffer.remaining() > 0) {
            channel.write(outSizeBuffer);
        }
    }

    public Header readHeader () throws IOException {
        int size = readSize ();

        headerBuffer.clear ();
        while (headerBuffer.position() != headerBuffer.capacity()) {
            channel.read (headerBuffer);
        }
        headerBuffer.flip();

        CUBRIDUnpacker unpacker = new CUBRIDUnpacker (headerBuffer);
        Header header = new Header (unpacker);
        if (size > headerBuffer.capacity()) {
            header.hasPayload = true;
            header.payloadSize = (size - headerBuffer.capacity());
        }
        return header;
    }

    public ByteBuffer read (int size) throws IOException {
        inBuffer = ByteBuffer.allocate(size);
        while (inBuffer.position() < size) {
            channel.read (inBuffer);
        }
        inBuffer.flip();
        return inBuffer;
    }

    public CUBRIDUnpacker readAndGetUnpacker (Header header) throws IOException {
        if (header.hasPayload == false || header.payloadSize <= 0) {
            return null;
        }

        ByteBuffer inputBuffer = read(header.payloadSize);
        inBuffer = null;
        return new CUBRIDUnpacker(inputBuffer);
    }

    public CUBRIDPacker getPacker (int size) {
        return new CUBRIDPacker (ByteBuffer.allocate(size));
    }
    
    public CUBRIDPacker getPacker () {
        return getPacker(DEFAULT_PACKER_SIZE);
    }

    public void write (ByteBuffer buf) throws IOException {
        key.interestOps(SelectionKey.OP_WRITE);
        writeSize (buf.position());

        buf.flip(); // write mode
        while (buf.remaining() > 0) {
            int bytes = channel.write(buf);
        }
        key.interestOps(SelectionKey.OP_READ);
    }

    public boolean isValid () {
        return channel.isConnected();
    }
    
    /*
    private static final float EXPAND_FACTOR = 2;
    private void ensureinBufferSpace(int size) {
        if (inBuffer.capacity() > size) {
            return;
        }
        int newCapacity = (int) (inBuffer.capacity() * EXPAND_FACTOR);
        while (newCapacity < (inBuffer.capacity() + size)) {
            newCapacity *= EXPAND_FACTOR;
        }
        ByteBuffer expanded = ByteBuffer.allocate(newCapacity);
        expanded.order(inBuffer.order());
        // expanded.put(inBuffer);
        inBuffer = expanded;
    }
    */
}
