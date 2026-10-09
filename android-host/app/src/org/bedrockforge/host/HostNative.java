package org.bedrockforge.host;
public final class HostNative {
 static {System.loadLibrary("bedrockforge_host");}
 public static native void start(String dataDirectory, String[] nativeModules);
}
