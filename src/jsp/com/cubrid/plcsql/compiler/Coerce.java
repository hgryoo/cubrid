/*
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

package com.cubrid.plcsql.compiler;

import com.cubrid.plcsql.compiler.ast.TypeSpec;
import com.cubrid.plcsql.compiler.ast.TypeSpecSimple;
import com.cubrid.plcsql.compiler.ast.TypeSpecVariadic;
import java.util.List;

public class Coerce {

    public String funcName;

    public Coerce() {}

    public Coerce(String funcName) {
        this.funcName = funcName;
    }

    public String toJavaCode(String exprJavaCode) {
        return String.format("%s(%s)", funcName, exprJavaCode);
    }

    public static Coerce getCoerce(TypeSpec from, TypeSpec to) {
        if (to.equals(TypeSpecSimple.OBJECT)
                || from.equals(TypeSpecSimple.NULL)
                || from.equals(to)) {
            return IDENTITY;
        }

        // TODO: fill other cases

        return null;
    }

    public static boolean matchTypeLists(List<TypeSpec> from, List<TypeSpec> to) {
        if (from.size() < to.size()) {
            return false;
        }

        boolean isDstVariadic = false;
        TypeSpec src, dst = null;
        int len = from.size();
        for (int i = 0; i < len; i++) {
            src = from.get(i);
            if (!isDstVariadic) {
                dst = to.get(i);
                if (dst instanceof TypeSpecVariadic) {
                    isDstVariadic = true;
                    dst = ((TypeSpecVariadic) dst).elem;
                }
            }
            assert dst != null;
            if (getCoerce(src, dst) == null) {
                return false;
            }
        }

        return true;
    }

    // ----------------------------------------------
    // cases
    // ----------------------------------------------

    private static class Identity extends Coerce {
        public String toJavaCode(String exprJavaCode) {
            return exprJavaCode; // no coercion
        }
    }

    private static Coerce IDENTITY = new Identity();
}