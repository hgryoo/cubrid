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
import com.sun.jna.Native;
import com.sun.jna.Platform;
import com.sun.jna.Structure;
import com.sun.jna.Union;
import com.sun.jna.ptr.IntByReference;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;

/**
 * Utility class to bridge native Unix domain socket calls to Java using JNA.
 */
public class UnixDomainSocketLibrary {
  public static final int PF_LOCAL = 1;
  public static final int AF_LOCAL = 1;
  public static final int SOCK_STREAM = 1;

  public static final int SHUT_RD = 0;
  public static final int SHUT_WR = 1;

  // Utility class, do not instantiate.
  private UnixDomainSocketLibrary() { }

  // BSD platforms write a length byte at the start of struct sockaddr_un.
  private static final boolean HAS_SUN_LEN =
      Platform.isMac() || Platform.isFreeBSD() || Platform.isNetBSD() ||
      Platform.isOpenBSD() || Platform.iskFreeBSD();

  /**
   * Bridges {@code struct sockaddr_un} to and from native code.
   */
  public static class SockaddrUn extends Structure implements Structure.ByReference {
    /**
     * On BSD platforms, the {@code sun_len} and {@code sun_family} values in
     * {@code struct sockaddr_un}.
     */
    public static class SunLenAndFamily extends Structure {
      public byte sunLen;
      public byte sunFamily;

      protected List getFieldOrder() {
        return Arrays.asList(new String[] { "sunLen", "sunFamily" });
      }
    }

    /**
     * On BSD platforms, {@code sunLenAndFamily} will be present.
     * On other platforms, only {@code sunFamily} will be present.
     */
    public static class SunFamily extends Union {
      public SunLenAndFamily sunLenAndFamily;
      public short sunFamily;
    }

    public SunFamily sunFamily = new SunFamily();
    public byte[] sunPath = new byte[104];

    /**
     * Constructs an empty {@code struct sockaddr_un}.
     */
    public SockaddrUn() {
      if (HAS_SUN_LEN) {
        sunFamily.sunLenAndFamily = new SunLenAndFamily();
        sunFamily.setType(SunLenAndFamily.class);
      } else {
        sunFamily.setType(Short.TYPE);
      }
      allocateMemory();
    }

    /**
     * Constructs a {@code struct sockaddr_un} with a path whose bytes are encoded
     * using the default encoding of the platform.
     */
    public SockaddrUn(String path) throws IOException {
      byte[] pathBytes = path.getBytes();
      if (pathBytes.length > sunPath.length - 1) {
        throw new IOException("Cannot fit name [" + path + "] in maximum unix domain socket length");
      }
      System.arraycopy(pathBytes, 0, sunPath, 0, pathBytes.length);
      sunPath[pathBytes.length] = (byte) 0;
      if (HAS_SUN_LEN) {
        int len = fieldOffset("sunPath") + pathBytes.length;
        sunFamily.sunLenAndFamily = new SunLenAndFamily();
        sunFamily.sunLenAndFamily.sunLen = (byte) len;
        sunFamily.sunLenAndFamily.sunFamily = AF_LOCAL;
        sunFamily.setType(SunLenAndFamily.class);
      } else {
        sunFamily.sunFamily = AF_LOCAL;
        sunFamily.setType(Short.TYPE);
      }
      allocateMemory();
    }

    protected List getFieldOrder() {
      return Arrays.asList(new String[] { "sunFamily", "sunPath" });
    }
  }

  static {
    Native.register(Platform.C_LIBRARY_NAME);
  }

  public static native int socket(int domain, int type, int protocol) throws LastErrorException;
  public static native int bind(int fd, SockaddrUn address, int addressLen)
    throws LastErrorException;
  public static native int listen(int fd, int backlog) throws LastErrorException;
  public static native int accept(int fd, SockaddrUn address, IntByReference addressLen)
    throws LastErrorException;
  public static native int connect(int fd, SockaddrUn address, int addressLen)
    throws LastErrorException;
  public static native int read(int fd, ByteBuffer buffer, int count)
    throws LastErrorException;
  public static native int write(int fd, ByteBuffer buffer, int count)
    throws LastErrorException;
  public static native int close(int fd) throws LastErrorException;
  public static native int shutdown(int fd, int how) throws LastErrorException;
}