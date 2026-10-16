package dev.hybridmcpe.launcher;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.content.res.Resources;
import android.content.res.AssetManager;
import android.content.res.loader.ResourcesLoader;
import android.content.res.loader.ResourcesProvider;
import android.database.sqlite.SQLiteDatabase;
import android.database.DatabaseErrorHandler;
import android.os.ParcelFileDescriptor;
import java.io.*;

/** Private per-version storage and public Android resource loading APIs only. */
public final class HostContext extends ContextWrapper {
    private final File data;
    private final String prefix;
    private final Resources resources;
    private final ClassLoader loader;
    private final ResourcesLoader resourcesLoader;

    public HostContext(Context base, ApkRepository.Version version, ClassLoader loader) throws IOException {
        super(base);
        data = new File(version.directory, "data");
        prefix = "game_" + version.id() + "_";
        this.loader = loader;
        resourcesLoader = new ResourcesLoader();
        for (File apk : version.apks()) {
            try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(apk, ParcelFileDescriptor.MODE_READ_ONLY)) {
                resourcesLoader.addProvider(ResourcesProvider.loadFromApk(fd));
            }
        }
        resources = new Resources(base.getAssets(), base.getResources().getDisplayMetrics(), base.getResources().getConfiguration());
        resources.addLoaders(resourcesLoader);
    }
    private File dir(String path) { File f = new File(data, path); if (!f.isDirectory() && !f.mkdirs()) throw new IllegalStateException("Cannot create private game directory"); return f; }
    private File child(File dir, String name) { if (name.contains("/") || name.contains("\\") || name.equals("..")) throw new IllegalArgumentException("Invalid private filename"); return new File(dir, name); }
    @Override public Context getApplicationContext() { return getBaseContext().getApplicationContext(); }
    @Override public Resources getResources() { return resources; }
    @Override public AssetManager getAssets() { return resources.getAssets(); }
    @Override public ClassLoader getClassLoader() { return loader; }
    @Override public File getFilesDir() { return dir("files"); }
    @Override public File getCacheDir() { return dir("cache"); }
    @Override public File getCodeCacheDir() { return dir("code-cache"); }
    @Override public File getNoBackupFilesDir() { return dir("no-backup"); }
    @Override public File getDataDir() { return data; }
    @Override public File getExternalFilesDir(String type) { return type == null ? dir("external/files") : childDir(type); }
    private File childDir(String type) { child(dir("external/files"), type); return dir("external/files/" + type); }
    @Override public File[] getExternalFilesDirs(String type) { return new File[]{getExternalFilesDir(type)}; }
    @Override public File getExternalCacheDir() { return dir("external/cache"); }
    @Override public File[] getExternalCacheDirs() { return new File[]{getExternalCacheDir()}; }
    @Override public File[] getObbDirs() { return new File[]{dir("obb")}; }
    @Override public File getObbDir() { return dir("obb"); }
    @Override public File getDir(String name, int mode) { child(data, name); return dir("app_" + name); }
    @Override public FileInputStream openFileInput(String name) throws FileNotFoundException { return new FileInputStream(child(getFilesDir(), name)); }
    @Override public FileOutputStream openFileOutput(String name, int mode) throws FileNotFoundException {
        if ((mode & ~(Context.MODE_APPEND)) != 0) throw new SecurityException("Private game storage only");
        return new FileOutputStream(child(getFilesDir(), name), (mode & Context.MODE_APPEND) != 0);
    }
    @Override public File getFileStreamPath(String name) { return child(getFilesDir(), name); }
    @Override public boolean deleteFile(String name) { return child(getFilesDir(), name).delete(); }
    @Override public String[] fileList() { String[] names = getFilesDir().list(); return names == null ? new String[0] : names; }
    @Override public SharedPreferences getSharedPreferences(String name, int mode) { return super.getSharedPreferences(prefix + name, Context.MODE_PRIVATE); }
    @Override public boolean deleteSharedPreferences(String name) { return super.deleteSharedPreferences(prefix + name); }
    @Override public File getDatabasePath(String name) { return child(dir("databases"), name); }
    @Override public SQLiteDatabase openOrCreateDatabase(String name, int mode, SQLiteDatabase.CursorFactory factory) {
        return SQLiteDatabase.openOrCreateDatabase(getDatabasePath(name), factory);
    }
    @Override public SQLiteDatabase openOrCreateDatabase(String name, int mode, SQLiteDatabase.CursorFactory factory, DatabaseErrorHandler errors) {
        return SQLiteDatabase.openOrCreateDatabase(getDatabasePath(name).getPath(), factory, errors);
    }
    @Override public boolean deleteDatabase(String name) { return SQLiteDatabase.deleteDatabase(getDatabasePath(name)); }
}
