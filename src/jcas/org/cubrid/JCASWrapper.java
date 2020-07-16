package org.cubrid;

import java.io.File;
import java.net.URL;

import com.cubrid.jsp.Server;

public class JCASWrapper {
	
	public static volatile CAS cas = CAS.INSTANCE;
	
	public static void main(String[] args) throws InterruptedException {
		System.out.println ("JCAS Initialized");
		
		final String[] arguments = args;

		
		for (int i = 0; i < arguments.length; i++) {
			System.out.println (arguments[i]);
		}
		
		Thread casThread = new Thread () {
			@Override
			public void run() {
				cas.main(arguments.length, arguments);
			}
		};
		casThread.start();
		
		Thread javspThread = new Thread () {
			@Override
			public void run() {
				String envvar = CAS.INSTANCE.envvar_root();
				String javaPath = envvar + "/java/jspserver.jar";
				String demoPath = envvar + "/demo";
				
				System.out.println ("demopath :" + demoPath);
				
				File file  = new File(javaPath);
				
				//cas.er_init(null, 0);

				URL url;
				try {
					url = file.toURI().toURL();
					URL[] urls = new URL[]{url};
					
					System.out.println (javaPath);
					//ClassLoader cl = new URLClassLoader(urls);
					//Class cls = cl.loadClass("com.cubrid.jsp.Server");
					Server server = new Server ("demodb", demoPath, "", envvar, "9000");
							//(Server) cls
							//.getDeclaredConstructor(String.class, String.class, String.class, String.class, String.class)
							//.newInstance("demodb", demoPath, "", envvar, "9000");
					//Server server = cls.newInstance();new Server ("", "", "", "", "9000");
					System.out.println ("Java SP server port: " + server.getServerPort());
				} catch (Exception e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
			
			}
		};
		javspThread.start();
		
		//cas.main(arguments.length, arguments);
		
		javspThread.join();
		System.out.println ("JCAS terminated");
	}
}