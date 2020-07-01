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
package cubrid.jdbc.jci.posix;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

import com.sun.jna.Pointer;

import cubrid.jdbc.jci.UJciException;
import cubrid.jdbc.jci.UTimedDataInputStream;

public class UShmDataInputStream extends UTimedDataInputStream {
	
	private static int IO_MAX_PAGE_SIZE = 16 * 1024;
	private static int MAX_ALIGNMENT = 8;
	private static int POSIX_SHM_CHUNK_SIZE = (IO_MAX_PAGE_SIZE + MAX_ALIGNMENT);
	private static int POSIX_SHM_CNT = 7;
	private static int SHM_TOTAL = (POSIX_SHM_CHUNK_SIZE * POSIX_SHM_CNT);
	
	private static String SEM_NAME_PRODUCE = "mutex-produce";
	private static String SEM_NAME_CONSUME = "mutex-consume";
	
	SharedMemoryPosix posix = null;
	
	int receivedSize = -1;
	ByteBuffer receivedBuffer = null;
	Pointer mem = null;
	
	int currentOffset = 0;
	boolean last = false;
	
	private static byte[] empty = {0, 0, 0, 0};
	
	boolean read_start = false;
	
	    public UShmDataInputStream(InputStream stream, String ip, int port) {
	        this(stream, ip, port, 0);
	    }

	    public UShmDataInputStream(InputStream stream, String ip, int port, int pid, byte session[], int timeout) {
	    	super (stream, ip, port, pid, session, timeout);
	    	posix = new SharedMemoryPosix("test_client", (long) SHM_TOTAL);
	    }

	    public UShmDataInputStream(InputStream stream, String ip, int port, int timeout) {
	    	super (stream, ip, port, timeout);
	    	posix = new SharedMemoryPosix("test_client", (long) SHM_TOTAL);
	    }

	    public int readInt(int timeout) throws IOException, UJciException {
	        int res = receivedBuffer.getInt(currentOffset);
	        currentOffset += Integer.BYTES;
	        return res;
	    }

	    public int readInt() throws IOException, UJciException {
	        return readInt(timeout);
	    }

	    public void readFully(byte[] b) throws IOException, UJciException {
	        readFully(b, timeout);
	    }

	    public boolean readMemory ()
	    {
	    	if (receivedBuffer == null || !receivedBuffer.hasRemaining() || currentOffset == receivedSize) 
	    	{
	    		if (!posix.semOpened()) {
	    			posix.openSem(SEM_NAME_PRODUCE, SEM_NAME_CONSUME);
	    		}
	    		
	    		posix.waitConsume();
	    		
	    		int chunk_size = 0;
	    		int chunk_idx = 0;
	    		
	    		posix.open(0, SHM_TOTAL);
	    		mem = posix.getMemory();
	    		
				ByteBuffer byteBuffer = ByteBuffer.wrap(mem.getByteArray(4, Integer.BYTES));
			    byteBuffer.order(ByteOrder.LITTLE_ENDIAN);
			    receivedSize = byteBuffer.getInt(0);
				
			    receivedBuffer = ByteBuffer.allocate(receivedSize);
			    receivedBuffer.put(mem.getByteArray(4 + Integer.BYTES, receivedSize));
	    		receivedBuffer.flip();

	    		currentOffset = 0;
	    		
	    		posix.close();
	    		posix.postProduce();
	    		//posix.closeSem();
	    	}
	    	
    		return true;
	    }
	    
    	byte[] magic = {67, 85, 66, 0};
    	byte[] endmagic = {67, 85, 67, 0};
	    public int readMagic (int idx)
	    {
	    	byte[] bytes = mem.getByteArray(idx * POSIX_SHM_CHUNK_SIZE, 4);

	    	if (java.util.Arrays.equals(bytes, magic)) {
	    		return 1;
	    	}
	    	else if (java.util.Arrays.equals(bytes, endmagic)) {
	    		return 2;
	    	}
	    	
	    	return -1;
	    }

	    public void readFully(byte[] b, int timeout) throws IOException, UJciException {
	        long begin = System.currentTimeMillis();
	        
	        //System.out.println ("array =" + b.length);
	        
	        int len = b.length;
	        
	        try {
	            //System.out.println ("remaining =" + receivedBuffer.remaining());
		        //System.out.println ("remaining2 =" + (receivedSize - currentOffset));
		        receivedBuffer.get(b);
		        currentOffset += len;

		        //System.out.println ("remaining =" + receivedBuffer.remaining());
		        ////System.out.println ("remaining2 =" + (receivedSize - currentOffset));
	        } catch (Exception e) {
	        	
	        	// parital read
	        	int offset = 0;
	        	
        		int partial_read = receivedBuffer.remaining();

        		//System.out.println ("reamining = " + partial_read);

        		receivedBuffer.get(b, 0, partial_read);
        		currentOffset += partial_read;
	        }
	    }

	    public int readByte(byte[] b, int timeout) throws IOException, UJciException {
	        long begin = System.currentTimeMillis();
	        
	        receivedBuffer.get(b, 0, 1);
	        currentOffset += 1;
	        
	        return 1;
	    }

	    public int readByte(byte[] b) throws IOException, UJciException {
	        return readByte(b, timeout);
	    }

	    public int read(byte[] b, int off, int len, int timeout) throws IOException, UJciException {
	        long begin = System.currentTimeMillis();
	        boolean retry = false;
	        
	        // System.out.println ("array =" + b.length + " offset = " + off + " len = " + len);

	        
	        try {
	        	//System.out.println ("remaining =" + len);
		        receivedBuffer.get(b, off, len);
		        currentOffset += len;
        		//System.out.println ("comsumed = " + (receivedSize - currentOffset));
	        } catch (Exception e) {
	        	//System.out.println ("READ EXCEPTION");
        		
	        	int partial_read = receivedBuffer.remaining();

        		//System.out.println ("reamining = " + partial_read);
        		//if (partial_read > remaining) {
        		//	partial_read = remaining;
        		//}

        		receivedBuffer.get(b, off, partial_read);
        		currentOffset += partial_read;
        		//System.out.println ("comsumed = " + (receivedSize - currentOffset));
	        	return partial_read;
	        }

	        return len;
	    }

	    public int read(byte[] b, int off, int len) throws IOException, UJciException {
	        return read(b, off, len, timeout);
	    }
}
