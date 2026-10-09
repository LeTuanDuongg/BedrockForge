package org.bedrockforge.host;

import android.app.Application;
import android.content.Context;
import android.content.res.Resources;
import android.content.res.AssetManager;
import android.content.res.loader.ResourcesLoader;
import android.content.res.loader.ResourcesProvider;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.nio.file.Files;

public final class HostApplication extends Application {
 @Override public void onCreate() {
  super.onCreate();
  try {
   ClassLoader loader=getClassLoader();
   if(loader instanceof dalvik.system.DexClassLoader) {
    loader.loadClass("com.google.android.gms.games.PlayGamesSdk")
      .getMethod("initialize",Context.class).invoke(null,this);
    loader.loadClass("com.google.firebase.FirebaseApp")
      .getMethod("initializeApp",Context.class).invoke(null,this);
   }
  } catch(Exception error) {throw new IllegalStateException("Game platform initialization failed",error);}
  registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks(){
   public void onActivityPreCreated(android.app.Activity activity,android.os.Bundle state){
    if(activity.getClass().getName().equals("com.mojang.minecraftpe.MainActivity")) {
     activity.setTheme(0x7f0e0007); // Theme from the hash-pinned installed APK manifest.
     HostNative.start(new File(getFilesDir(),"mod-data").getPath());
    }
   }
   public void onActivityCreated(android.app.Activity a,android.os.Bundle b){
    if(a.getClass().getName().equals("com.mojang.minecraftpe.MainActivity"))android.util.Log.i("BedrockForge","Minecraft activity started in the isolated host process");
   }
   public void onActivityStarted(android.app.Activity a){}
   public void onActivityResumed(android.app.Activity a){}
   public void onActivityPaused(android.app.Activity a){}
   public void onActivityStopped(android.app.Activity a){}
   public void onActivitySaveInstanceState(android.app.Activity a,android.os.Bundle b){}
   public void onActivityDestroyed(android.app.Activity a){}
  });
 }
 @Override protected void attachBaseContext(Context base) {
  super.attachBaseContext(base);
  try {
   String process = new String(Files.readAllBytes(new File("/proc/self/cmdline").toPath())).trim();
   if (process.startsWith("org.bedrockforge.host:game")) {
    File root=new File(getFilesDir().getParentFile(),"game-runtime");
    File apk=new File(new String(Files.readAllBytes(new File(root,"apk-path").toPath())).trim());
    ResourcesLoader loader=new ResourcesLoader();
    try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(apk,ParcelFileDescriptor.MODE_READ_ONLY)) {
     loader.addProvider(ResourcesProvider.loadFromApk(fd));
    }
    getResources().addLoaders(loader);
   }
  } catch(Exception error) { throw new IllegalStateException("Game resources unavailable",error); }
 }
}

