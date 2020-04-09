/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.DataOutputStream;
import java.io.IOException;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;

import cubrid.jdbc.driver.CUBRIDException;

public class UConnectionServer extends UConnection {
	
	protected Thread curThread;
	protected ConcurrentHashMap <String, List<UStatement>> stmtHandlerCache;
	
	UConnectionServer(Socket socket, Thread curThread) throws CUBRIDException {
		errorHandler = new UError(this);
		try {
			client = socket;
			client.setTcpNoDelay(true);
			
			output = new DataOutputStream(client.getOutputStream());
			output.writeInt(0x08);
			output.flush();
			input = new UTimedDataInputStream(client.getInputStream(), CASIp, CASPort);
			
			needReconnection = false;
			lastAutoCommit = false;
			this.curThread = curThread;
			
			initConnectionInfo ();
			
			stmtHandlerCache = new ConcurrentHashMap <String, List<UStatement>>();
			
			UJCIUtil.invoke("com.cubrid.jsp.StoredProcedureHandler", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { connectionProperties.getCharSet() });
		} catch (IOException e) {
		    	UJciException je = new UJciException(UErrorCode.ER_CONNECTION);
		    	je.toUError(errorHandler);
			throw new CUBRIDException(errorHandler, e);
		}
	}
	
	private void initConnectionInfo () {
		casinfo = new byte[CAS_INFO_SIZE];
		casinfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
		casinfo[CAS_INFO_RESERVED_1] = 0;
		casinfo[CAS_INFO_RESERVED_2] = 0;
		casinfo[CAS_INFO_ADDITIONAL_FLAG] = 0;

		/* create default broker info */
		broker_info = new byte[BROKER_INFO_SIZE];
		broker_info[BROKER_INFO_DBMS_TYPE] = DBMS_CUBRID;
		broker_info[BROKER_INFO_RESERVED4] = 0;
		broker_info[BROKER_INFO_STATEMENT_POOLING] = 1;
		broker_info[BROKER_INFO_CCI_PCONNECT] = 0;
		broker_info[BROKER_INFO_PROTO_VERSION] 
				= CAS_PROTO_INDICATOR | CAS_PROTOCOL_VERSION;
		broker_info[BROKER_INFO_FUNCTION_FLAG] 
				= CAS_RENEWED_ERROR_CODE | CAS_SUPPORT_HOLDABLE_RESULT;
		broker_info[BROKER_INFO_RESERVED2] = 0;
		broker_info[BROKER_INFO_RESERVED3] = 0;

		brokerVersion = makeProtoVersion(CAS_PROTOCOL_VERSION);
	}
	
	@Override
	public void setCharset(String newCharsetName) {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.StoredProcedureHandler", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { newCharsetName });
		}
	}

	@Override
	public void setZeroDateTimeBehavior(String behavior) throws CUBRIDException {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.StoredProcedureHandler",
					"setZeroDateTimeBehavior", new Class[] { String.class },
					this.curThread, new Object[] { behavior });
		}
	}

	@Override
	public void setResultWithCUBRIDTypes(String support) throws CUBRIDException {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.StoredProcedureHandler",
					"setResultWithCUBRIDTypes", new Class[] { String.class },
					this.curThread, new Object[] { support });
		}
	}
	
	@Override
	public void setAutoCommit(boolean autoCommit) {
		/* do nothing */
	}
	
	@Override
	public boolean protoVersionIsAbove(int ver) {
		return true;
	}

	@Override
	protected void closeInternal() {
		if (client != null) {
			disconnect();
		}
	}
	
	@Override
	protected void checkReconnect() throws IOException, UJciException {
		if (dbInfo == null) {
			dbInfo = createDBInfo(dbname, user, passwd, url);
		}
		// set the session id
		if (brokerInfoVersion() == 0) {
			/* Interpretable session information supporting version 
			*   later than PROTOCOL_V3 as well as version earlier 
			*   than PROTOCOL_V3 should be delivered since no broker information 
			*   is provided at the time of initial connection.
			*/
			String id = "0";
			UJCIUtil.copy_bytes(dbInfo, 608, 20, id);
		} else if (protoVersionIsAbove(PROTOCOL_V3)) {
			System.arraycopy(sessionId, 0, dbInfo, 608, 20);
		} else {
		    	UJCIUtil.copy_bytes(dbInfo, 608, 20, new Integer(oldSessionId).toString());
		}
	}
	
	public List<UStatement> getStmtHandlerCache (String query) {
		if (stmtHandlerCache.get(query) == null) {
			List<UStatement> stmts = new ArrayList<UStatement>();
			
		} else {
			
		}
		
		return null;
	}
	
	public void putStmtHandlerCache (String query, UStatement stmt) {
		List<UStatement> stmts = null;
		
		stmts = stmtHandlerCache.get(query);
		if (stmts == null) {
			stmts = new ArrayList<UStatement>();
			stmts.add(stmt);
		}
	}
}
