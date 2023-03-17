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
package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import java.util.Arrays;
import org.antlr.v4.runtime.ParserRuleContext;

public class DeclCursor extends DeclId {

    @Override
    public <R> R accept(AstVisitor<R> visitor) {
        return visitor.visitDeclCursor(this);
    }

    public final String name;
    public final NodeList<DeclParam> paramList;
    public final ExprStr sql;
    public final NodeList<ExprId> usedVars;

    public int[] paramRefCounts;
    public int[] usedValuesMap;

    public DeclCursor(
            ParserRuleContext ctx,
            String name,
            NodeList<DeclParam> paramList,
            ExprStr sql,
            NodeList<ExprId> usedVars) {
        super(ctx);

        assert paramList != null;
        this.name = name;
        this.paramList = paramList;
        this.sql = sql;
        this.usedVars = usedVars;

        setHostValuesMap(paramList, usedVars);
    }

    @Override
    public String kind() {
        return "cursor";
    }

    @Override
    public String toJavaCode() {
        return String.format(
                "final Query %s = new Query(%s);\n  // param-ref-counts: %s\n  // used-values-map: %s",
                name,
                sql.toJavaCode(),
                Arrays.toString(paramRefCounts),
                Arrays.toString(usedValuesMap));
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

    private void setHostValuesMap(NodeList<DeclParam> paramList, NodeList<ExprId> usedVars) {

        int paramSize = paramList.nodes.size();
        int usedSize = usedVars == null ? 0 : usedVars.nodes.size();

        paramRefCounts = new int[paramSize]; // NOTE: filled with zeros
        usedValuesMap = new int[usedSize]; // NOTE: filled with zeros

        for (int i = 0; i < paramSize; i++) {
            DeclParam di = paramList.nodes.get(i);
            for (int j = 0; j < usedSize; j++) {
                DeclId dj = usedVars.nodes.get(j).decl;
                if (di == dj) { // NOTE: reference equality
                    usedValuesMap[j] = -(i + 1);
                    paramRefCounts[i]++;
                }
            }
        }
    }
}