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

package com.cubrid.jsp.base;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.ByteBuffer;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;

public class MockTCPClient {

        private Socket socket;

        private CUBRIDPacker packer;
        private CUBRIDUnpacker unpacker;

        private InputStream inputStream;
        private OutputStream outputStream;

        private DataOutputStream output;
        private DataInputStream input;

        private long sessionid = 0;

        public MockTCPClient (int port) throws IOException {
                InetAddress addr = InetAddress.getLocalHost();
                socket = new Socket(addr.getHostAddress(), port);

                inputStream = socket.getInputStream();
                outputStream = socket.getOutputStream();

                packer = new CUBRIDPacker(ByteBuffer.allocate(1024));
                unpacker = new CUBRIDUnpacker();

                output = new DataOutputStream(new BufferedOutputStream(outputStream));
        }

        public void setId (long id) {
                sessionid = id;
        }

        public long getId () {
                return sessionid;
        }

        public CUBRIDPacker getPacker () {
                return packer;
        }

        public CUBRIDUnpacker getUnPacker () {
                return unpacker;
        }

        public void sendAndReceive () throws IOException {
                send ();
                receive ();
        }

        private void send () throws IOException {
                ByteBuffer buffer = packer.getBuffer();

                output.writeInt(buffer.position());
                output.write(buffer.array(), 0, buffer.position());
                output.flush();

                buffer.clear();
        }

        private void receive () throws IOException {
                if (input == null) {
                input = new DataInputStream(new BufferedInputStream(inputStream));
                }

                int size = input.readInt(); // size
                byte[] bytes = new byte[size];
                input.readFully(bytes);

                ByteBuffer buffer = ByteBuffer.wrap(bytes);
                unpacker.setBuffer(buffer);
        }

        public void close () throws IOException {
                socket.close();
        }
}
