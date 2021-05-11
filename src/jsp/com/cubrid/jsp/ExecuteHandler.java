/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */
package com.cubrid.jsp;

import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.Value;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.sql.CUBRIDOID;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.SocketChannel;
import java.sql.ResultSet;
import java.text.SimpleDateFormat;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

public class ExecuteHandler implements Runnable {

    private String charSet = System.getProperty("file.encoding");

    final SocketChannel socketChannel;
    final SelectionKey selectionKey;

    /* DB Types */
    public static final int DB_NULL = 0;
    public static final int DB_INT = 1;
    public static final int DB_FLOAT = 2;
    public static final int DB_DOUBLE = 3;
    public static final int DB_STRING = 4;
    public static final int DB_OBJECT = 5;
    public static final int DB_SET = 6;
    public static final int DB_MULTISET = 7;
    public static final int DB_SEQUENCE = 8;
    public static final int DB_TIME = 10;
    public static final int DB_TIMESTAMP = 11;
    public static final int DB_DATE = 12;
    public static final int DB_MONETARY = 13;
    public static final int DB_SHORT = 18;
    public static final int DB_NUMERIC = 22;
    public static final int DB_CHAR = 25;
    public static final int DB_RESULTSET = 28;
    public static final int DB_BIGINT = 31;
    public static final int DB_DATETIME = 32;

    private static final int REQ_CODE_INVOKE_SP = 0x01;
    private static final int REQ_CODE_RESULT = 0x02;
    private static final int REQ_CODE_ERROR = 0x04;
    private static final int REQ_CODE_INTERNAL_JDBC = 0x08;
    private static final int REQ_CODE_DESTROY = 0x10;
    private static final int REQ_CODE_END = 0x20;

    private static final int REQ_CODE_UTIL_PING = 0xDE;
    private static final int REQ_CODE_UTIL_STATUS = 0xEE;
    private static final int REQ_CODE_UTIL_TERMINATE_THREAD = 0xFE;
    private static final int REQ_CODE_UTIL_TERMINATE_SERVER = 0xFF; // to shutdown javasp server

    ByteBuffer input = ByteBuffer.allocateDirect(1024);
    StoredProcedure procedure;

    static final int READING = 0, SENDING = 1;
    int state = READING;

    String clientName = "";
    private AtomicInteger status = new AtomicInteger(ExecuteThreadStatus.IDLE.getValue());

    Value result = null;

    ExecuteHandler(Selector selector, SocketChannel c) throws IOException {
        socketChannel = c;
        socketChannel.configureBlocking(false);
        selectionKey = socketChannel.register(selector, 0);
        selectionKey.attach(this);
        selectionKey.interestOps(SelectionKey.OP_READ);
        selector.wakeup();
    }

    public void run() {
        try {
            System.out.println("run");
            if (state == READING) {
                read();
            } else if (state == SENDING) {
                send();
            }
        } catch (Exception ex) {
            ex.printStackTrace();
        }
    }

    void read() throws IOException {
        System.out.println("read");
        int readCount = socketChannel.read(input);
        if (readCount > 0) {
            input.flip();
            try {
                System.out.println("listenCommand");
                int requestCode = listenCommand();
                switch (requestCode) {
                        /* the following two request codes are for processing java stored procedure routine */
                    case REQ_CODE_INVOKE_SP:
                        {
                            System.out.println("processStoredProcedure");
                            result = processStoredProcedure();
                            setStatus(ExecuteThreadStatus.RESULT);
                            break;
                        }

                        /* the following request codes are for javasp utility */
                    case REQ_CODE_UTIL_PING:
                        {
                            setStatus(ExecuteThreadStatus.PING);
                            break;
                        }
                    case REQ_CODE_UTIL_STATUS:
                        {
                            setStatus(ExecuteThreadStatus.STATUS);
                            break;
                        }
                    case REQ_CODE_UTIL_TERMINATE_THREAD:
                        {
                            // Thread.currentThread().interrupt();
                            break;
                        }
                    case REQ_CODE_UTIL_TERMINATE_SERVER:
                        {
                            Server.stop(0);
                            break;
                        }

                        /* invalid request */
                    default:
                        {
                            // throw new ExecuteException ("invalid request code: " + requestCode);
                        }
                }
            } catch (Throwable e) {
                if (e instanceof IOException) {
                    setStatus(ExecuteThreadStatus.END);
                    /*
                     * CAS disconnects socket
                     * 1) end of the procedure successfully by calling jsp_close_internal_connection ()
                     * 2) socket is in invalid status. we do not have to deal with it here.
                     */
                    // break;
                } else {
                    try {
                        // closeJdbcConnection();
                    } catch (Exception e2) {
                    }
                    setStatus(ExecuteThreadStatus.ERROR);
                    Throwable throwable = e;
                    if (e instanceof InvocationTargetException) {
                        throwable = ((InvocationTargetException) e).getTargetException();
                    }
                    Server.log(throwable);
                    /*
                    try {
                        sendError(throwable.toString(), client);
                    } catch (IOException e1) {
                        Server.log(e1);
                    }
                    */
                }
            }
            input.clear();
        }
        state = SENDING;
        // Interested in writing
        selectionKey.interestOps(SelectionKey.OP_WRITE);
    }

    /**
     * Processing of the read message. This only prints the message to stdOut.
     *
     * @param readCount
     */
    synchronized void readProcess(int readCount) {
        StringBuilder sb = new StringBuilder();
        input.flip();
        byte[] subStringBytes = new byte[readCount];
        byte[] array = input.array();
        System.arraycopy(array, 0, subStringBytes, 0, readCount);
        // Assuming ASCII (bad assumption but simplifies the example)
        sb.append(new String(subStringBytes));
        input.clear();
        clientName = sb.toString().trim();
    }

    void send() throws IOException, ExecuteException, TypeMismatchException {
        System.out.println("send");
        ByteBuffer output = null;
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        DataOutputStream w = new DataOutputStream(baos);

        ExecuteThreadStatus status = getStatus();
        System.out.println("send " + status);
        switch (status) {
            case RESULT:
                {
                    ByteArrayOutputStream resultStream = getResultStream(result, procedure);

                    w.writeInt(REQ_CODE_RESULT);
                    w.writeInt(byteBuf.size() + 4);
                    resultStream.writeTo(w);
                    w.writeInt(REQ_CODE_RESULT);
                    setStatus(ExecuteThreadStatus.IDLE);
                    break;
                }
            case PING:
                {
                    String ping = Server.getServerName();
                    w.writeInt(ping.length());
                    w.writeBytes(ping);
                    break;
                }
            case STATUS:
                {
                    String dbName = Server.getServerName();
                    List<String> vm_args = Server.getJVMArguments();
                    int length = getLengthtoSend(dbName) + 12;
                    for (String arg : vm_args) {
                        length += getLengthtoSend(arg) + 4;
                    }

                    w.writeInt(length);
                    w.writeInt(Server.getServerPort());
                    packAndSendRawString(dbName, w);

                    w.writeInt(vm_args.size());
                    for (String arg : vm_args) {
                        packAndSendRawString(arg, w);
                    }
                }
                break;

            default:
        }

        output = ByteBuffer.wrap(baos.toByteArray());
        socketChannel.write(output);

        setStatus(ExecuteThreadStatus.IDLE);
        baos.reset();
        w.flush();
        selectionKey.cancel();
    }

    private int listenCommand() throws IOException {
        setStatus(ExecuteThreadStatus.IDLE);
        return input.getInt();
    }

    private Value processStoredProcedure() throws Exception {
        setStatus(ExecuteThreadStatus.PARSE);
        procedure = makeStoredProcedure();
        Method m = procedure.getTarget().getMethod();

        Object[] resolved = procedure.checkArgs(procedure.getArgs());

        setStatus(ExecuteThreadStatus.INVOKE);
        // Object result = m.invoke(null, resolved);
        Object result = procedure.getTarget().invoke(resolved);

        /* close server-side JDBC connection */
        // closeJdbcConnection();

        /* send results */
        Value resolvedResult = procedure.makeReturnValue(result);

        System.out.println ("Value: " + resolvedResult);

        return resolvedResult;
    }

    private StoredProcedure makeStoredProcedure() throws Exception {
        byte len = input.get();
        int methodSigLength = 0;
        if (len == 0xff) {
            methodSigLength = input.getInt();
        } else {
            methodSigLength = (int) len;
        }

        byte[] methodSig = new byte[methodSigLength];
        input.get(methodSig);
        input.get();

        int paramCount = input.getInt();
        Value[] args = PackerNG.getArguments(input, paramCount);

        // TODO
        int returnType = input.getInt();

        int endCode = input.getInt();
        if (endCode != REQ_CODE_INVOKE_SP) {
            return null;
        }

        procedure = new StoredProcedure(new String(methodSig), args, returnType);
        return procedure;
    }

    public void setStatus(int value) {
        this.status.set(value);
    }

    public void setStatus(ExecuteThreadStatus value) {
        this.status.set(value.getValue());
    }

    public ExecuteThreadStatus getStatus() {
        return ExecuteThreadStatus.get(status.get());
    }

    public boolean compareStatus(ExecuteThreadStatus value) {
        return (status.get() == value.getValue());
    }

    ByteArrayOutputStream byteBuf = new ByteArrayOutputStream(1024);

    private ByteArrayOutputStream getResultStream(Value result, StoredProcedure procedure)
            throws IOException, ExecuteException, TypeMismatchException {

        DataOutputStream w = new DataOutputStream(byteBuf);

        Object resolvedResult = null;
        if (result != null) {
            resolvedResult = toDbTypeValue(procedure.getReturnType(), result);
        }

        byteBuf.reset();
        sendValue(resolvedResult, w, procedure.getReturnType());
        returnOutArgs(procedure, w);

        return byteBuf;
    }

    private void sendValue(Object result, DataOutputStream dos, int ret_type)
            throws IOException, ExecuteException {
        if (result == null) {
            dos.writeInt(DB_NULL);
        } else if (result instanceof Short) {
            dos.writeInt(DB_SHORT);
            dos.writeInt(((Short) result).intValue());
        } else if (result instanceof Integer) {
            dos.writeInt(DB_INT);
            dos.writeInt(((Integer) result).intValue());
        } else if (result instanceof Long) {
            dos.writeInt(DB_BIGINT);
            dos.writeLong(((Long) result).longValue());
        } else if (result instanceof Float) {
            dos.writeInt(DB_FLOAT);
            dos.writeFloat(((Float) result).floatValue());
        } else if (result instanceof Double) {
            dos.writeInt(ret_type);
            dos.writeDouble(((Double) result).doubleValue());
        } else if (result instanceof BigDecimal) {
            dos.writeInt(DB_NUMERIC);
            packAndSendString(((BigDecimal) result).toString(), dos);
        } else if (result instanceof String) {
            dos.writeInt(DB_STRING);
            packAndSendString((String) result, dos);
        } else if (result instanceof java.sql.Date) {
            dos.writeInt(DB_DATE);
            packAndSendString(result.toString(), dos);
        } else if (result instanceof java.sql.Time) {
            dos.writeInt(DB_TIME);
            packAndSendString(result.toString(), dos);
        } else if (result instanceof java.sql.Timestamp) {
            dos.writeInt(ret_type);

            if (ret_type == DB_DATETIME) {
                packAndSendString(result.toString(), dos);
            } else {
                SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
                packAndSendString(formatter.format(result), dos);
            }
        } else if (result instanceof CUBRIDOID) {
            dos.writeInt(DB_OBJECT);
            byte[] oid = ((CUBRIDOID) result).getOID();
            dos.writeInt(UJCIUtil.bytes2int(oid, 0));
            dos.writeInt(UJCIUtil.bytes2short(oid, 4));
            dos.writeInt(UJCIUtil.bytes2short(oid, 6));
        } else if (result instanceof ResultSet) {
            dos.writeInt(DB_RESULTSET);
            // dos.writeInt(((CUBRIDResultSet) result).getServerHandle());
        } else if (result instanceof int[]) {
            int length = ((int[]) result).length;
            Integer[] array = new Integer[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Integer(((int[]) result)[i]);
            }
            sendValue(array, dos, ret_type);
        } else if (result instanceof short[]) {
            int length = ((short[]) result).length;
            Short[] array = new Short[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Short(((short[]) result)[i]);
            }
            sendValue(array, dos, ret_type);
        } else if (result instanceof float[]) {
            int length = ((float[]) result).length;
            Float[] array = new Float[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Float(((float[]) result)[i]);
            }
            sendValue(array, dos, ret_type);
        } else if (result instanceof double[]) {
            int length = ((double[]) result).length;
            Double[] array = new Double[length];
            for (int i = 0; i < array.length; i++) {
                array[i] = new Double(((double[]) result)[i]);
            }
            sendValue(array, dos, ret_type);
        } else if (result instanceof Object[]) {
            dos.writeInt(ret_type);
            Object[] arr = (Object[]) result;

            dos.writeInt(arr.length);
            for (int i = 0; i < arr.length; i++) {
                sendValue(arr[i], dos, ret_type);
            }
        } else ;
    }

    private int getLengthtoSend(String str) throws IOException {
        byte b[] = str.getBytes();

        int len = b.length + 1;

        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        return len + pad;
    }

    private void packAndSendRawString(String str, DataOutputStream dos) throws IOException {
        byte b[] = str.getBytes();

        int len = b.length + 1;
        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        dos.writeInt(len + pad);
        dos.write(b);
        for (int i = 0; i <= pad; i++) {
            dos.writeByte(0);
        }
    }

    private void packAndSendString(String str, DataOutputStream dos) throws IOException {
        byte b[] = str.getBytes(this.charSet);

        int len = b.length + 1;
        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        dos.writeInt(len + pad);
        dos.write(b);
        for (int i = 0; i <= pad; i++) {
            dos.writeByte(0);
        }
    }

    private Object toDbTypeValue(int dbType, Value result) throws TypeMismatchException {
        Object resolvedResult = null;

        if (result == null) return null;

        switch (dbType) {
            case DB_INT:
                resolvedResult = result.toIntegerObject();
                break;
            case DB_BIGINT:
                resolvedResult = result.toLongObject();
                break;
            case DB_FLOAT:
                resolvedResult = result.toFloatObject();
                break;
            case DB_DOUBLE:
            case DB_MONETARY:
                resolvedResult = result.toDoubleObject();
                break;
            case DB_CHAR:
            case DB_STRING:
                resolvedResult = result.toString();
                break;
            case DB_SET:
            case DB_MULTISET:
            case DB_SEQUENCE:
                resolvedResult = result.toObjectArray();
                break;
            case DB_TIME:
                resolvedResult = result.toTime();
                break;
            case DB_DATE:
                resolvedResult = result.toDate();
                break;
            case DB_TIMESTAMP:
                resolvedResult = result.toTimestamp();
                break;
            case DB_DATETIME:
                resolvedResult = result.toDatetime();
                break;
            case DB_SHORT:
                resolvedResult = result.toShortObject();
                break;
            case DB_NUMERIC:
                resolvedResult = result.toBigDecimal();
                break;
            case DB_OBJECT:
                resolvedResult = result.toOid();
                break;
            case DB_RESULTSET:
                resolvedResult = result.toResultSet();
                break;
            default:
                break;
        }

        return resolvedResult;
    }

    private void returnOutArgs(StoredProcedure sp, DataOutputStream dos)
            throws IOException, ExecuteException, TypeMismatchException {
        Value[] args = sp.getArgs();
        for (int i = 0; i < args.length; i++) {
            if (args[i].getMode() > Value.IN) {
                Value v = makeOutBingValue(sp, args[i].getResolved());
                sendValue(toDbTypeValue(args[i].getDbType(), v), dos, args[i].getDbType());
            }
        }
    }

    private Value makeOutBingValue(StoredProcedure sp, Object object) throws ExecuteException {
        Object obj = null;
        if (object instanceof byte[]) {
            obj = new Byte(((byte[]) object)[0]);
        } else if (object instanceof short[]) {
            obj = new Short(((short[]) object)[0]);
        } else if (object instanceof int[]) {
            obj = new Integer(((int[]) object)[0]);
        } else if (object instanceof long[]) {
            obj = new Long(((long[]) object)[0]);
        } else if (object instanceof float[]) {
            obj = new Float(((float[]) object)[0]);
        } else if (object instanceof double[]) {
            obj = new Double(((double[]) object)[0]);
        } else if (object instanceof byte[][]) {
            obj = ((byte[][]) object)[0];
        } else if (object instanceof short[][]) {
            obj = ((short[][]) object)[0];
        } else if (object instanceof int[][]) {
            obj = ((int[][]) object)[0];
        } else if (object instanceof long[][]) {
            obj = ((long[][]) object)[0];
        } else if (object instanceof float[][]) {
            obj = ((float[][]) object)[0];
        } else if (object instanceof double[][]) {
            obj = ((double[][]) object)[0];
        } else if (object instanceof Object[]) {
            obj = ((Object[]) object)[0];
        }

        return sp.makeReturnValue(obj);
    }
}
