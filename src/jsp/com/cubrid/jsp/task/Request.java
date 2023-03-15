package com.cubrid.jsp.task;

import java.nio.ByteBuffer;

public class Request {
    public int reqeustId;
    public ByteBuffer requestBuffer;

    public Request (int id, ByteBuffer buffer) {
        this.reqeustId = id;
        this.requestBuffer = buffer;
    }
}
