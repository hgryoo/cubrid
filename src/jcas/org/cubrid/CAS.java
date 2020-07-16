/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
package org.cubrid;

import java.util.Arrays;
import java.util.List;

import com.sun.jna.Library;
import com.sun.jna.Memory;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;

public interface CAS extends Library {
	CAS INSTANCE = Native.loadLibrary("cub_cas_shared", CAS.class);
	
	public int main(int arc, String[] argv);
	
	/* environment */
	public String envvar_root();
	public boolean prm_get_bool_value (int prm_id);
	
	
	public int ux_prepare (String sql_stmt, int flag, byte auto_commit_mode, CAS.T_NET_BUF net_buf, CAS.T_REQ_INFO.ByReference req_info, int query_seq_num);
	public int er_init (Pointer msglog_filename, int exit_ask);
	
	public static class T_NET_BUF extends Structure {
		public static class ByReference extends T_NET_BUF implements Structure.ByReference { }
		public int alloc_size;
		public int data_size;
		public Pointer data;
		public int err_code;
		public int post_file_size;
		public Pointer post_send_file;
		public int client_version;
		
		public T_NET_BUF () {
			data = null;
			alloc_size = 0;
			data_size = 0;
			err_code = 0;
			post_file_size = 0;
			post_send_file = null;
			client_version = 0;
		}
		
		/*
		public T_NET_BUF (int size) {
			data = new Memory(size);
			alloc_size = size;
			data_size = 0;
			err_code = 0;
			post_file_size = 0;
			post_send_file = null;
			client_version = 0;
		}
		*/

		@Override
		protected List getFieldOrder() {
			return Arrays.asList("alloc_size", "data_size","data",
					"err_code","post_file_size","post_send_file", "client_version");
		}
	}
	
	public static class T_REQ_INFO extends Structure {
		public static class ByReference extends T_REQ_INFO implements Structure.ByReference { }
		public int client_version;
		public byte[] driver_info;
		public int need_auto_commit;
		public byte need_rollback;

		public T_REQ_INFO () {
			client_version = 0;
			driver_info = new byte[10];
			need_auto_commit = 0;
			need_rollback = 0;
		}

		@Override
		protected List getFieldOrder() {
			return Arrays.asList("client_version", "driver_info","need_auto_commit",
					"need_rollback");
		}

	}
	
	/* fr protocol */
	public int fr_get_cursor_count (int srv_h_id);
	public int fr_get_cursor_pos (int srv_h_id);
	public int fr_move_cursor (int srv_h_id, int cursor_pos, int origin);
	public int fr_peek_cursor (int srv_h_id, char fetch_flag, T_NET_BUF.ByReference net_buf);

}