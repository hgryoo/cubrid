package com.cubrid.jsp.data;

public class ColumnInfo {

    public String className;
    public String attrName;

    public short scale;
    public int prec;
    public byte charset;
    public String colName;
    public byte isNotNull;

    public byte autoIncrement;
    public byte uniqueKey;
    public byte primaryKey;
    public byte reverseIndex;
    public byte reverseUnique;
    public byte foreignKey;
    public byte shared;
    public String defaultValueString;

    public ColumnInfo (CUBRIDUnpacker unpacker) {
        charset = (byte) unpacker.unpackInt();
        scale = unpacker.unpackShort();
        prec = unpacker.unpackInt();
        
        colName = unpacker.unpackCString();
        attrName = unpacker.unpackCString();
        className = unpacker.unpackCString();
        defaultValueString = unpacker.unpackCString();

        isNotNull = (byte) unpacker.unpackInt();
        autoIncrement = (byte) unpacker.unpackInt();
        uniqueKey = (byte) unpacker.unpackInt();
        primaryKey = (byte) unpacker.unpackInt();
        reverseIndex = (byte) unpacker.unpackInt();
        reverseUnique = (byte) unpacker.unpackInt();
        foreignKey = (byte) unpacker.unpackInt();
        shared = (byte) unpacker.unpackInt();
    }
}
