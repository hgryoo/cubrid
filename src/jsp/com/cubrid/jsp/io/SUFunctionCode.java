package com.cubrid.jsp.io;

public enum SUFunctionCode {

    PREPARE(2),
    EXECUTE(3),
    GET_DB_PARAMETER(4),
    CURSOR(7),
    FETCH(8),
    GET_SCHEMA_INFO(9),
    GET_BY_OID(10),
    PUT_BY_OID(11),
    GET_DB_VERSION(15),
    RELATED_TO_OID(17),
    RELATED_TO_COLLECTION(18),
    NEXT_RESULT(19),
    EXECUTE_BATCH_STATEMENT(20),
    EXECUTE_BATCH_PREPAREDSTATEMENT(21), 
    CURSOR_UPDATE(22),
    MAKE_OUT_RS(33),

    GET_GENERATED_KEYS(34),

    NEW_LOB(35),
    WRITE_LOB(36),
    READ_LOB(37),
    PREPARE_AND_EXECUTE(41),
    CURSOR_CLOSE(42),

    LAST_FUNCTION_CODE (-1);

    private int code;

    SUFunctionCode(int code) {
        this.code = code;
    }

    SUFunctionCode(SUFunctionCode code) {
        this.code = code.getCode();
    }

    public int getCode() {
        return this.code;
    }
}
