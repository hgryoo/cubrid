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
/**
 * 
 */
package com.cubrid.jsp;

/**
 * @author hgryoo
 *
 */
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketAddress;
import java.util.concurrent.atomic.AtomicInteger;

import com.sun.jna.LastErrorException;
import com.sun.jna.ptr.IntByReference;

/**
 * Implements a {@link ServerSocket} which binds to a local Unix domain socket
 * and returns instances of {@link UnixDomainSocket} from
 * {@link #accept()}.
 */
public class UnixDomainServerSocket extends ServerSocket {
  private static final int DEFAULT_BACKLOG = 50;

  // We use an AtomicInteger to prevent a race in this situation which
  // could happen if fd were just an int:
  //
  // Thread 1 -> UnixDomainServerSocket.accept()
  //          -> lock this
  //          -> check isBound and isClosed
  //          -> unlock this
  //          -> descheduled while still in method
  // Thread 2 -> UnixDomainServerSocket.close()
  //          -> lock this
  //          -> check isClosed
  //          -> UnixDomainSocketLibrary.close(fd)
  //          -> now fd is invalid
  //          -> unlock this
  // Thread 1 -> re-scheduled while still in method
  //          -> UnixDomainSocketLibrary.accept(fd, which is invalid and maybe re-used)
  //
  // By using an AtomicInteger, we'll set this to -1 after it's closed, which
  // will cause the accept() call above to cleanly fail instead of possibly
  // being called on an unrelated fd (which may or may not fail).
  private final AtomicInteger fd;

  private final int backlog;
  private boolean isBound;
  private boolean isClosed;

  public static class UnixDomainServerSocketAddress extends SocketAddress {
    private final String path;

    public UnixDomainServerSocketAddress(String path) {
      this.path = path;
    }

    public String getPath() {
      return path;
    }
  }

  /**
   * Constructs an unbound Unix domain server socket.
   */
  public UnixDomainServerSocket() throws IOException {
    this(DEFAULT_BACKLOG, null);
  }

  /**
   * Constructs an unbound Unix domain server socket with the specified listen backlog.
   */
  public UnixDomainServerSocket(int backlog) throws IOException {
    this(backlog, null);
  }

  /**
   * Constructs and binds a Unix domain server socket to the specified path.
   */
  public UnixDomainServerSocket(String path) throws IOException {
    this(DEFAULT_BACKLOG, path);
  }

  /**
   * Constructs and binds a Unix domain server socket to the specified path
   * with the specified listen backlog.
   */
  public UnixDomainServerSocket(int backlog, String path) throws IOException {
    try {
      fd = new AtomicInteger(
          UnixDomainSocketLibrary.socket(
              UnixDomainSocketLibrary.PF_LOCAL,
              UnixDomainSocketLibrary.SOCK_STREAM,
              0));
      this.backlog = backlog;
      if (path != null) {
        bind(new UnixDomainServerSocketAddress(path));
      }
    } catch (LastErrorException e) {
      throw new IOException(e);
    }
  }

  public synchronized void bind(SocketAddress endpoint) throws IOException {
    if (!(endpoint instanceof UnixDomainServerSocketAddress)) {
      throw new IllegalArgumentException(
          "endpoint must be an instance of UnixDomainServerSocketAddress");
    }
    if (isBound) {
      throw new IllegalStateException("Socket is already bound");
    }
    if (isClosed) {
      throw new IllegalStateException("Socket is already closed");
    }
    UnixDomainServerSocketAddress unEndpoint = (UnixDomainServerSocketAddress) endpoint;
    UnixDomainSocketLibrary.SockaddrUn address =
        new UnixDomainSocketLibrary.SockaddrUn(unEndpoint.getPath());
    try {
      int socketFd = fd.get();
      UnixDomainSocketLibrary.bind(socketFd, address, address.size());
      UnixDomainSocketLibrary.listen(socketFd, backlog);
      isBound = true;
    } catch (LastErrorException e) {
      e.printStackTrace();
      throw new IOException(e);
    }
  }

  public Socket accept() throws IOException {
    // We explicitly do not make this method synchronized, since the
    // call to UnixDomainSocketLibrary.accept() will block
    // indefinitely, causing another thread's call to close() to deadlock.
    synchronized (this) {
      if (!isBound) {
        throw new IllegalStateException("Socket is not bound");
      }
      if (isClosed) {
        throw new IllegalStateException("Socket is already closed");
      }
    }
    try {
      UnixDomainSocketLibrary.SockaddrUn sockaddrUn =
          new UnixDomainSocketLibrary.SockaddrUn();
      IntByReference addressLen = new IntByReference();
      addressLen.setValue(sockaddrUn.size());
      int clientFd = UnixDomainSocketLibrary.accept(fd.get(), sockaddrUn, addressLen);
      return new UnixDomainSocket(clientFd);
    } catch (LastErrorException e) {
      throw new IOException(e);
    }
  }

  public synchronized void close() throws IOException {
    if (isClosed) {
      throw new IllegalStateException("Socket is already closed");
    }
    try {
      // Ensure any pending call to accept() fails.
      UnixDomainSocketLibrary.close(fd.getAndSet(-1));
      isClosed = true;
    } catch (LastErrorException e) {
      throw new IOException(e);
    }
  }
}