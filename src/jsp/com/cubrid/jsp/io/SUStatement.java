package com.cubrid.jsp.io;
import com.cubrid.jsp.data.ColumnInfo;
import com.cubrid.jsp.data.ResultInfo;
import com.cubrid.jsp.data.GetByOIDInfo;
import com.cubrid.jsp.data.GetSchemaInfo;
import com.cubrid.jsp.data.PrepareInfo;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConstants;

import cubrid.jdbc.jci.CUBRIDCommandType;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public class SUStatement {

    private static final byte NORMAL = 0,
    GET_BY_OID = 1,
    GET_SCHEMA_INFO = 2,
    GET_AUTOINCREMENT_KEYS = 3;

    private static final int DEFAULT_FETCH_SIZE = 100;

    private int handlerId = -1;
    private int type = NORMAL;

    /* prepare info */
    private int columnNumber;
    private int parameterNumber;
    private byte commandType;
    private byte firstStmtType;
    private HashMap<String, Integer> colNameToIndex;
    private SUBindParameter bindParameter;

    private Object columnInfo[];
    private Object tuples[];

    private byte executeFlag;
    private boolean isGeneratedKeys = false;

	private boolean isSensitive;

    /* fetch info */
    private ResultInfo resultInfo[] = null;

    /* related to fetch */
    private int maxFetchSize;
    private int fetchSize = DEFAULT_FETCH_SIZE;

    int cursorPosition = 0;
    int totalTupleNumber = 0;
    int fetchedTupleNumber = 0;
    int currentFirstCursor = -1;

    boolean isFetched = false;

    SUConnection suConn;
    public SUStatement (SUConnection conn, PrepareInfo info, boolean recompile, String sql, byte flag) {
        suConn = conn;
        
        maxFetchSize = 0;
        isSensitive = false;

        handlerId = info.handleId;
        commandType = info.stmtType;
        firstStmtType = commandType;
        parameterNumber = info.numParameters;

        setColumnInfo (info.columnInfos);
        fetchSize = DEFAULT_FETCH_SIZE;
        currentFirstCursor = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;

        maxFetchSize = 0;
        isFetched= false;
        /*
        if (info.stmtType == CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
            columnNumber = parameterNumber + 1;
        }
        */
    }

    public SUStatement (SUConnection conn, GetByOIDInfo info, String attributeName[]) {
        type = GET_BY_OID;
        handlerId = -1;

        fetchSize = 1;
        maxFetchSize = 0;
        isSensitive = false;
    }

    public SUStatement (SUConnection conn, GetSchemaInfo info, String cName, String attributePattern, int type)
    {
        type = GET_SCHEMA_INFO;
        handlerId = -1;

        fetchSize = 1;
        maxFetchSize = 0;
        isSensitive = false;
    }

    public boolean getSQLType () {
        switch (commandType)
        {
            case CUBRIDCommandType.CUBRID_STMT_SELECT:
            case CUBRIDCommandType.CUBRID_STMT_CALL:
            case CUBRIDCommandType.CUBRID_STMT_GET_STATS:
            case CUBRIDCommandType.CUBRID_STMT_EVALUATE:
                return true;
        }
        return false;
    }

    public ResultInfo getResultInfo (int idx) {
        if (resultInfo == null) {
            return null;
        }

        if (idx < 0 || idx >= resultInfo.length) {
            return null;
        }

        return resultInfo[idx];
    }

    public void execute (
        int maxRow,
        int maxField,
        boolean isExecuteAll,
        boolean isSensitive,
        boolean isScrollable
    ) throws IOException {

        if (type == GET_SCHEMA_INFO) {
            return;
        }

        if (bindParameter != null && !bindParameter.checkAllBinded()) {
            // TODO: error handling
            return;
        }

        setExecuteFlags (maxRow, isExecuteAll, isSensitive);
        currentFirstCursor = -1;
        fetchedTupleNumber = 0;

        /* TODO
        if (firstStmtType == CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
            cursorPosition = 0;
        } else 
        */
        {
            cursorPosition = -1;
        }

        suConn.execute(handlerId, executeFlag, isScrollable, maxField, bindParameter);
    }

    private void setColumnInfo (List<ColumnInfo> columnInfos) {
        colNameToIndex = new HashMap<String, Integer>(columnNumber);
        for (int i = 0; i < columnInfos.size(); i++) {
            String name = columnInfos.get(i).colName.toLowerCase();
            if (colNameToIndex.containsKey(name) == false) {
                colNameToIndex.put(name, i);
            }
        }
    }

    private void setExecuteFlags (
        int maxRow,
        boolean isExecuteAll,
        boolean isSensitive
    ) {
        executeFlag = 0;

        if (isExecuteAll) {
            executeFlag |= CUBRIDServerSideConstants.EXEC_FLAG_QUERY_ALL;
        }

        if (isGeneratedKeys) {
            executeFlag |= CUBRIDServerSideConstants.EXEC_FLAG_GET_GENERATED_KEYS;
        }

        this.isSensitive = isSensitive;
        this.maxFetchSize = maxRow;
    }

    public void setAutoGeneratedKeys (boolean autoGeneratedKeys) {
        this.isGeneratedKeys = autoGeneratedKeys;
    }

    public byte getStatementType () {
        return commandType;
    } 

    public void fetch () {
        if (type == GET_BY_OID) {
            return;
        }

        /* need not to request fetch */
        /*
            if (currentFirstCursor >= 0
            && currentFirstCursor <= cursorPosition
            && cursorPosition <= currentFirstCursor + fetchedTupleNumber - 1) {
        return;
        */

    }

    public SOID executeInsert () throws IOException {
        /*
        if (commandTypeIs != CUBRIDCommandType.CUBRID_STMT_INSERT) {
            errorHandler.setErrorCode(UErrorCode.ER_CMD_IS_NOT_INSERT);
            return null;
        }
        */
        execute (0, 0, false, false, false);

        if (resultInfo != null && resultInfo[0] != null) {
            // TODO
            // return resultInfo[0].getCUBRIDOID();
            return null;
        }

        return null;
    }

}
