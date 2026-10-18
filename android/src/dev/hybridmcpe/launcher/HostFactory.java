package dev.hybridmcpe.launcher;

import android.app.AppComponentFactory;
import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.util.Log;
import dalvik.system.DexClassLoader;
import java.io.*;
import java.util.*;

/** A real Activity loaded from unmodified imported DEX, not an external launch Intent. */
public final class HostFactory extends AppComponentFactory {
    public static ApkRepository.Version version;
    public static ClassLoader gameLoader;

    @Override public ClassLoader instantiateClassLoader(ClassLoader parent, ApplicationInfo app) {
        if (!Application.getProcessName().endsWith(":game")) return parent;
        try {
            File files = new File(app.dataDir, "files");
            String id;
            try (DataInputStream in = new DataInputStream(new FileInputStream(new File(files, "host-selection")))) { id = in.readUTF(); }
            if (!id.matches("[a-f0-9]{64}")) throw new IOException("Invalid selected version");
            ApkRepository repo = new ApkRepository(new File(files, "versions"), android.os.Build.VERSION.SDK_INT, (h,s,m) -> "");
            for (ApkRepository.Version v : repo.list()) if (v.id().equals(id)) version = v;
            if (version == null) throw new IOException("Selected version unavailable");
            repo.verify(version);
            if (!"android.app.Application".equals(version.info.getProperty("application"))) throw new IOException("Custom game Application lifecycle is not supported yet");
            if (!"com.mojang.minecraftpe.MainActivity".equals(version.info.getProperty("mainActivity"))) throw new IOException("Unsupported entry Activity");
            StringJoiner dex = new StringJoiner(File.pathSeparator);
            dex.add(app.sourceDir);
            for (File f : version.apks()) dex.add(f.getAbsolutePath());
            String nativePath = new File(version.directory, "native/arm64-v8a").getPath() + File.pathSeparator + app.nativeLibraryDir;
            // Only the subclass must resolve in the game loader, alongside its real superclass.
            gameLoader = new DexClassLoader(dex.toString(), new File(version.directory, "data/code-cache").getPath(), nativePath, parent) {
                @Override protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
                    String activity = "dev.hybridmcpe.launcher.HostedMinecraftActivity";
                    if (!name.equals(activity) && !name.startsWith(activity + "$")) return super.loadClass(name, resolve);
                    synchronized (this) {
                        Class<?> c = findLoadedClass(name);
                        if (c == null) c = findClass(name);
                        if (resolve) resolveClass(c);
                        return c;
                    }
                }
            };
            Thread.currentThread().setContextClassLoader(gameLoader);
            Class<?> notifications = Class.forName("com.mojang.minecraftpe.NotificationListenerService", false, gameLoader);
            Log.i("HybridMCPE", "host_stage=notification_class_resolved loader=" + notifications.getClassLoader());
            Log.i("HybridMCPE", "host_stage=verified_apk_set pid=" + android.os.Process.myPid() + " engine=" + version.info.getProperty("engine.sha256"));
            return gameLoader;
        } catch (Exception e) {
            Log.e("HybridMCPE", "host_stage=classloader_failed; original checks unchanged", e);
            throw new IllegalStateException("Imported game host preparation failed", e);
        }
    }
}
