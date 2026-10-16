package dev.hybridmcpe.launcher;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/** Calls the real imported MainActivity lifecycle, with isolated host storage. */
public final class HostedMinecraftActivity extends com.mojang.minecraftpe.MainActivity {
    private boolean diagnosticLoaded;
    private boolean diagnosticActive;
    private boolean destroyed;
    @Override protected void attachBaseContext(Context base) {
        try { super.attachBaseContext(new HostContext(base, HostFactory.version, HostFactory.gameLoader)); }
        catch (Exception e) { throw new IllegalStateException("Game context setup failed", e); }
    }
    @Override public void onCreate(Bundle state) {
        Log.i("HybridMCPE", "host_stage=original_activity_onCreate_begin pid=" + android.os.Process.myPid());
        int theme = (int)Long.parseUnsignedLong(HostFactory.version.info.getProperty("theme", "0"));
        if (theme != 0) setTheme(theme);
        super.onCreate(state);
        Log.i("HybridMCPE", "host_stage=original_activity_onCreate_returned; menu_not_yet_verified pid=" + android.os.Process.myPid());
        if (getIntent().getBooleanExtra("diagnostics", false)) {
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                if (destroyed) return;
                String result = MainActivity.startDiagnostic(getApplicationInfo().nativeLibraryDir + "/libhybrid_diagnostic.so");
                diagnosticLoaded = result.endsWith(";diagnostic_load=0");
                diagnosticActive = diagnosticLoaded;
                Log.i("HybridMCPE", "host_stage=diagnostic_attempt pid=" + android.os.Process.myPid() + " result=" + result);
            }, 15000);
        }
    }
    @Override public void onPause() {
        Log.i("HybridMCPE", "host_stage=pause pid=" + android.os.Process.myPid());
        if (diagnosticActive) {
            int code = MainActivity.diagnosticLifecycle(false);
            if (code == 0) diagnosticActive = false;
            Log.i("HybridMCPE", "diagnostic_suspend=" + code);
        }
        super.onPause();
    }
    @Override public void onResume() {
        super.onResume();
        if (diagnosticLoaded && !diagnosticActive) {
            int code = MainActivity.diagnosticLifecycle(true);
            diagnosticActive = code == 0;
            Log.i("HybridMCPE", "diagnostic_resume=" + code);
        }
        Log.i("HybridMCPE", "host_stage=resume pid=" + android.os.Process.myPid());
    }
    @Override public void onDestroy() {
        destroyed = true;
        if (diagnosticLoaded) Log.i("HybridMCPE", "diagnostic_shutdown=" + MainActivity.stopDiagnostic());
        super.onDestroy();
        Log.i("HybridMCPE", "host_stage=destroy pid=" + android.os.Process.myPid());
    }
}
