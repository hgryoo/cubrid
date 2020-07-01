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

import com.sun.jna.Platform;
import com.sun.jna.Pointer;

/**
 * Created by Mitchell Skaggs on 5/16/2019.
 */

abstract class MMAN {
    static final int PROT_READ;
    static final int PROT_WRITE;
    static final int MAP_SHARED;
    static final Pointer MAP_FAILED = new Pointer(-1);

    static {
        switch (Platform.getOSType()) {
            case Platform.MAC:
            case Platform.FREEBSD:
            case Platform.OPENBSD:
            case Platform.KFREEBSD:
            case Platform.NETBSD:

                // Source: https://github.com/nneonneo/osx-10.9-opensource/blob/master/xnu-2422.1.72/bsd/sys/mman.h

                PROT_READ = 0x01;
                PROT_WRITE = 0x02;
                MAP_SHARED = 0x0001;

                break;
            default:

                // Source: /usr/include/x86_64-linux-gnu/bits/mman-linux.h

                PROT_READ = 0x1;
                PROT_WRITE = 0x2;
                MAP_SHARED = 0x01;

                break;
        }
    }
}