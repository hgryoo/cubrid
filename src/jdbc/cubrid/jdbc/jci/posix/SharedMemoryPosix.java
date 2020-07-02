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
import com.sun.jna.Native;
import com.sun.jna.Pointer;

import static com.sun.jna.Pointer.NULL;
import static cubrid.jdbc.jci.posix.FCNTL.O_CREAT;
import static cubrid.jdbc.jci.posix.FCNTL.O_RDWR;
import static cubrid.jdbc.jci.posix.FCNTL.O_RDONLY;
import static cubrid.jdbc.jci.posix.MMAN.*;
import static cubrid.jdbc.jci.posix.STAT.S_IRUSR;
import static cubrid.jdbc.jci.posix.STAT.S_IWUSR;

public class SharedMemoryPosix implements SharedMemory {
	
	private static int fileDescriptor = -1;
	
    private Pointer memory;
    
    private Pointer mutex_produce;
    private Pointer mutex_consume;
    
    private long size;
    private String name;
    private int idx = -1;

    private boolean closed;
    private boolean hasOwnership;
    
    private boolean sem_opened = false;
    
	public SharedMemoryPosix(String name, long size) {
        this.name = name;
        
        if (fileDescriptor == -1) {
	        fileDescriptor = LibRT.INSTANCE.shm_open(this.name, O_RDONLY, S_IRUSR | S_IWUSR);
	        if (fileDescriptor < 0) {
	            fileDescriptor = LibRT.INSTANCE.shm_open(this.name, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR);
	            hasOwnership = true;
	        }
	        if (fileDescriptor < 0)
	            throw new RuntimeException(LibC.INSTANCE.strerror(Native.getLastError()));
	
	        if (hasOwnership) {
	            int ftruncateCode = LibC.INSTANCE.ftruncate(fileDescriptor, size);
	            if (ftruncateCode < 0)
	                throw new RuntimeException(LibC.INSTANCE.strerror(Native.getLastError()));
	        }
        }
    }
	
	public void openSem (String produce, String consume) {
		mutex_produce = Semaphore.INSTANCE.sem_open(produce, O_CREAT);
		mutex_consume = Semaphore.INSTANCE.sem_open(consume, O_CREAT);
		sem_opened = true;
	}
	
	public void waitSem (Pointer mutex) {
		Semaphore.INSTANCE.sem_wait(mutex);
	}
	
	public void waitConsume () {
		Semaphore.INSTANCE.sem_wait(mutex_consume);
	}
	
	public void postProduce () {
		Semaphore.INSTANCE.sem_post(mutex_produce);
	}
	
	public void postSem (Pointer mutex) {
		Semaphore.INSTANCE.sem_post(mutex);
	}
	
	public boolean semOpened () {
		return sem_opened;
	}
	
	public void closeSem () {
		Semaphore.INSTANCE.sem_close(mutex_produce);
		Semaphore.INSTANCE.sem_close(mutex_consume);
	}
	
	public Pointer getProduceMutex () {
		return mutex_produce;
	}
	
	public Pointer getConsumeMutex () {
		return mutex_consume;
	}
	
	
	public void open (int idx, long size) {
		this.size = size;
        closed = false;
        //fileDescriptor = LibRT.INSTANCE.shm_open(this.name, O_RDWR, S_IRUSR | S_IWUSR);
        memory = LibRT.INSTANCE.mmap(NULL, size, PROT_READ, MAP_SHARED, fileDescriptor, 0);
        if (memory.equals(MAP_FAILED))
            throw new RuntimeException(LibC.INSTANCE.strerror(Native.getLastError()));
        
        this.idx = idx;
	}

	@Override
	public void close() {
		// TODO Auto-generated method stub
		if (closed)
            return;
		
	        int munmapCode = LibRT.INSTANCE.munmap(memory, this.size);
	        if (munmapCode < 0)
	            throw new RuntimeException(LibC.INSTANCE.strerror(Native.getLastError()));
	
	        memory = null;
	        closed = true;
	}

	@Override
	public Pointer getMemory() {
		// TODO Auto-generated method stub
		return memory;
	}

}
