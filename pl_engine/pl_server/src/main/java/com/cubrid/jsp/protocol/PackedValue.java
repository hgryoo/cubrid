package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;

public class PackedValue implements PackableObject {

        private Value value;

        public PackedValue (Value v) {
                value = v;
        }

        @Override
        public void pack(CUBRIDPacker packer) {
                // TODO Auto-generated method stub
                throw new UnsupportedOperationException("Unimplemented method 'pack'");
        }
        
}
