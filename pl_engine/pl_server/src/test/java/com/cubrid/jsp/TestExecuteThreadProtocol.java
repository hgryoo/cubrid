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

package com.cubrid.jsp;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;

import java.io.IOException;
import java.nio.file.Path;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import com.cubrid.jsp.base.MockTCPClient;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.protocol.Header;
import com.cubrid.jsp.protocol.RequestCode;

public class TestExecuteThreadProtocol {
    private Server testServer = null;

    @AfterAll
    public static void stopServer() {
        Server.stop (0);
    }

    @BeforeAll
    public static void setUp(@TempDir Path tempDir) throws ClassNotFoundException, IOException {
        ServerConfig config =
                new ServerConfig(
                        "mock",
                        "1.0",
                        tempDir.toAbsolutePath().toString(),
                        tempDir + "/databases",
                        "5152");
        Server.startWithConfig(config);
    }

    @Test
    public void testServerExists() {
        testServer = Server.getServer();
        assertNotNull(testServer);
    }

    private String sendPing (MockTCPClient client) throws IOException {
        Header header = new Header (client.getId(), RequestCode.UTIL_PING, -1);
        header.pack (client.getPacker());

        client.sendAndReceive();

        CUBRIDUnpacker unpacker = client.getUnPacker();
        return unpacker.unpackCString();
    }

    private String sendRequest (MockTCPClient client, int code) throws IOException {
        Header header = new Header (client.getId(), code, -1);
        header.pack (client.getPacker());

        client.sendAndReceive();

        CUBRIDUnpacker unpacker = client.getUnPacker();
        return unpacker.unpackCString();
    }

    @Test
    public void testPing () throws IOException {
        MockTCPClient client = new MockTCPClient (5152);

        assertEquals("mock", sendPing (client));

        client.close();
    }

    @Test
    public void testPingMany () throws IOException {
        MockTCPClient client = new MockTCPClient (5152);
        client.setId(1);

        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));
        assertEquals("mock", sendPing (client));

        client.close();
    }

    @Test
    public void testStatus () throws IOException {
        MockTCPClient client = new MockTCPClient (5152);
        client.setId(2);

        assertEquals("mock", sendPing (client));

        client.close();
    }

}
