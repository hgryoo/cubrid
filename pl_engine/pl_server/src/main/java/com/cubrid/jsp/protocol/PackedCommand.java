package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.DataUtilities;

public class PackedCommand implements PackableObject {

        private int code;

        public PackedCommand (int code) {
                this.code = code;
        }
        
        @Override
        public void pack(CUBRIDPacker packer) {
                packer.packInt(code);
                packer.align(DataUtilities.MAX_ALIGNMENT);

        }
        
}
