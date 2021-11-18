/*
 * Copyright (C) 2008 Search Solution Corporation.
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

package cubrid.jdbc.jci;

import java.util.Properties;

public class UUserInfo {

    private String brokerName;
    private String casName;
    private String dbName;
    private String dbUser;
    private String ipAddr;

    private Properties properties = null;

    UUserInfo(String bName, String cName, String name, String user, String ip) {
        brokerName = bName;
        casName = cName;
        dbName = name;
        dbUser = user;
        ipAddr = ip;
    }

    public String getBrokerName() {
        return brokerName;
    }

    public String getCasName() {
        return casName;
    }

    public String getDbName() {
        return dbName;
    }

    public String getIpAddress() {
        return ipAddr;
    }

    public String getDbUser() {
        return dbUser;
    }

    public Properties toProperties() {
        if (properties == null) {
            properties = new Properties();
            properties.put("broker", brokerName);
            properties.put("client", casName);
            properties.put("db", dbName);
            properties.put("user", dbUser);
            properties.put("ip", ipAddr);
        }
        return properties;
    }
}
