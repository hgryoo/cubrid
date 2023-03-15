package com.cubrid.jsp.handler;

import com.cubrid.jsp.Server;
import com.cubrid.jsp.Server;
import com.cubrid.jsp.StoredProcedure;
import com.cubrid.jsp.communication.ConnectionEntry;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.context.ProcedureStack;
import com.cubrid.jsp.context.ProcedureStack;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.RequestCode;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.InvocationTargetException;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.util.List;

public class DispatchHandler implements Runnable {

    SelectionKey key = null;

    public DispatchHandler(SelectionKey key) {
        this.key = key;
    }

    @Override
    public void run() {
        ConnectionEntry connEntry = null;
        CUBRIDUnpacker unpacker = null;
        CUBRIDPacker packer = null;
        try {
            connEntry = (ConnectionEntry) key.attachment();

            // read header
            Header header = connEntry.readHeader();
            if (header == null) {
                throw new RuntimeException ();
            }

            Context ctx = ContextManager.getContext(header.id);
            if (ctx == null) {
                // error, should not happen
                throw new RuntimeException ();
            }
            ContextManager.registerThread(Thread.currentThread().getId(), ctx.getSessionId());

            unpacker = connEntry.readAndGetUnpacker (header);
            packer = connEntry.getPacker();

            // dispatch
            switch (header.code) {
                case RequestCode.UTIL_PING:
                    {
                        processPing(packer);
                        break;
                    }
                case RequestCode.UTIL_STATUS:
                    {
                        processStatus(packer);
                        break;
                    }
                case RequestCode.UTIL_TERMINATE_SERVER:
                    {
                        Server.stop(0);
                        break;
                    }
                case RequestCode.PREPARE_ARGS:
                    {
                        processPrepare(ctx, unpacker, packer);
                        break;
                    }
                case RequestCode.INVOKE_SP:
                    {
                        ContextManager.registerThread(Thread.currentThread().getId(), ctx.getSessionId());
                        processInvoke(ctx, unpacker, packer);
                        break;
                    }
                case RequestCode.INTERNAL_JDBC:
                    {
                        ctx.getInboundQueue().put (unpacker.unpackBuffer());
                        break;
                    }
                default:
                    {
                        // error;
                        throw new RuntimeException ();
                    }
            }
        } catch (Exception e) {
            if (e instanceof IOException) {
                // expected socket closed
                return;
            }

            Throwable throwable = e;
            if (e instanceof InvocationTargetException) {
                throwable = ((InvocationTargetException) e).getTargetException();
            }
            processError (throwable, packer);
            Server.log (e);
        } finally {
            ContextManager.deregisterThread(Thread.currentThread().getId());

            if (packer != null && packer.getPosition() > 0) {
                try {
                    connEntry.write(packer.getBuffer());
                } catch (IOException e) {
                    // ignore the exception socket closed
                }
            }
            key.interestOps(SelectionKey.OP_READ);
        }
    }

    private void processPing(CUBRIDPacker packer) {
        // a ping to check javasp server is running
        String ping = Server.getServerName();
        packer.packString(ping);
    }

    private void processStatus(CUBRIDPacker packer) {
        // TODO: create a packable class for status
        packer.packInt(Server.getServerPort());
        packer.packString(Server.getServerName());
        List<String> vm_args = Server.getJVMArguments();
        packer.packInt(vm_args.size());
        for (String arg : vm_args) {
            packer.packString(arg);
        }
    }

    private void processPrepare(Context ctx, CUBRIDUnpacker unpacker, CUBRIDPacker packer)
            throws TypeMismatchException {
        long groupId = unpacker.unpackBigint();

        ProcedureStack group = ctx.getProcedureStack(groupId);

        int argCount = unpacker.unpackInt();
        Value[] arguments = new Value[argCount];
        for (int i = 0; i < arguments.length; i++) {
            int paramType = unpacker.unpackInt();

            Value arg = unpacker.unpackValue(paramType);
            arguments[i] = (arg);
        }

        group.setArguments(arguments);
        packer.packInt(0);
    }

    private void processInvoke(Context ctx, CUBRIDUnpacker unpacker, CUBRIDPacker packer)
            throws Exception {
        long groupId = unpacker.unpackBigint();

        ProcedureStack currentStack = ctx.getProcedureStack(groupId);
        currentStack.setConnectionEntry ((ConnectionEntry) key.attachment());

        String methodSig = unpacker.unpackCString();
        int paramCount = unpacker.unpackInt();

        Value[] preparedArgs = currentStack.getArguments();
        Value[] methodArgs = null;
        if (paramCount > 0) {
            methodArgs = new Value[paramCount];
            for (int i = 0; i < paramCount; i++) {
                int pos = unpacker.unpackInt();
                int mode = unpacker.unpackInt();
                int type = unpacker.unpackInt();

                Value val = preparedArgs[pos];
                val.setMode(mode);
                val.setDbType(type);

                methodArgs[i] = val;
            }
        }
        int returnType = unpacker.unpackInt();

        StoredProcedure procedure = ctx.getProcedure(methodSig);
        Value result = procedure.invoke(methodArgs);
        if (result != null) {
            packer.packInt(RequestCode.RESULT);
            Object resolvedResult = ValueUtilities.resolveValue(returnType, result);
            packer.packValue(resolvedResult, returnType, ctx.getCharset());
            returnOutArgs(ctx, procedure, packer);
        } else {
            throw new RuntimeException ();
        }
    }

    private void processError(Throwable e, CUBRIDPacker packer) {
        packer.clear();

        packer.packInt(RequestCode.ERROR);
        packer.packString(e.toString());
    }

    private void returnOutArgs(Context ctx, StoredProcedure sp, CUBRIDPacker packer)
            throws ExecuteException, TypeMismatchException, UnsupportedEncodingException {
        Value[] args = sp.getArgs();
        if (args != null) {
            for (int i = 0; i < args.length; i++) {
                if (args[i].getMode() > Value.IN) {
                    Value v = sp.makeOutValue(args[i].getResolved());
                    packer.packValue(
                            ValueUtilities.resolveValue(args[i].getDbType(), v),
                            args[i].getDbType(),
                            ctx.getCharset());
                }
            }
        }
    }
}
