package org.bedrockforge.host;

import android.app.AppComponentFactory;
import android.content.pm.ApplicationInfo;
import dalvik.system.DexClassLoader;
import java.io.File;
import java.nio.file.Files;

/** Loads unmodified, user-installed game code only in the isolated game process. */
public final class GameFactory extends AppComponentFactory {
 @Override public ClassLoader instantiateClassLoader(ClassLoader parent, ApplicationInfo info) {
  try {
   String process = new String(Files.readAllBytes(new File("/proc/self/cmdline").toPath())).trim();
   if (!process.startsWith("org.bedrockforge.host:game")) return parent;
   File root = new File(info.dataDir, "game-runtime");
   String apk = new String(Files.readAllBytes(new File(root,"apk-path").toPath())).trim();
   if (!new File(apk).isFile()) throw new IllegalStateException("Installed Minecraft APK unavailable");
   ClassLoader game = new DexClassLoader(apk, new File(info.dataDir,"code_cache").getPath(),
       new File(root,"lib").getPath(), parent);
   // Preserve the installed APK's component-factory initialization before activities.
   Class.forName("androidx.core.app.CoreComponentFactory", true, game);
   return game;
  } catch (Exception error) { throw new IllegalStateException("Minecraft class loader preparation failed",error); }
 }
}

