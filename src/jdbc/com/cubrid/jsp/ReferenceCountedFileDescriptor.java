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
package com.cubrid.jsp;

import com.sun.jna.LastErrorException;

import java.io.IOException;

/**
 * Encapsulates a file descriptor plus a reference count to ensure close requests
 * only close the file descriptor once the last reference to the file descriptor
 * is released.
 *
 * If not explicitly closed, the file descriptor will be closed when
 * this object is finalized.
 */
public class ReferenceCountedFileDescriptor {
  private int fd;
  private int fdRefCount;
  private boolean closePending;

  public ReferenceCountedFileDescriptor(int fd) {
    this.fd = fd;
    this.fdRefCount = 0;
    this.closePending = false;
  }

  protected void finalize() throws IOException {
    close();
  }

  public synchronized int acquire() {
    fdRefCount++;
    return fd;
  }

  public synchronized void release() throws IOException {
    fdRefCount--;
    if (fdRefCount == 0 && closePending && fd != -1) {
      doClose();
    }
  }

  public synchronized void close() throws IOException {
    if (fd == -1 || closePending) {
      return;
    }

    if (fdRefCount == 0) {
      doClose();
    } else {
      // Another thread has the FD. We'll close it when they release the reference.
      closePending = true;
    }
  }

  private void doClose() throws IOException {
    try {
      UnixDomainSocketLibrary.close(fd);
      fd = -1;
    } catch (LastErrorException e) {
      throw new IOException(e);
    }
  }
}