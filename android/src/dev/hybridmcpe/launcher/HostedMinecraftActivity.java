package dev.hybridmcpe.launcher;

import android.content.Context;
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/** Calls the real imported MainActivity lifecycle, with isolated host storage. */
public final class HostedMinecraftActivity extends com.mojang.minecraftpe.MainActivity {
    private boolean diagnosticLoaded;
    private boolean diagnosticActive;
    private boolean destroyed;
    private boolean resumed;
    private BroadcastReceiver experimentalTrigger;
    private void registerExperimentalTrigger() {
        if (!MainActivity.experimentalEnabled() || !getIntent().getBooleanExtra("experimental", false) ||
                !"dev.hybridmcpe.launcher".equals(getPackageName())) return;
        experimentalTrigger = new BroadcastReceiver() {
            @Override public void onReceive(Context context, Intent intent) {
                if (!"dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE".equals(intent.getAction())) return;
                Log.i("HybridMCPE", "registry_trigger=request;elapsed_ns=" +
                    android.os.SystemClock.elapsedRealtimeNanos() + ";pid=" + android.os.Process.myPid() +
                    ";tid=" + android.os.Process.myTid());
                int result = !destroyed && resumed && diagnosticActive ?
                    MainActivity.triggerExperimentalRegistryProbe() : -1;
                Log.i("HybridMCPE", "registry_trigger=received;result=" + result +
                    ";elapsed_ns=" + android.os.SystemClock.elapsedRealtimeNanos() +
                    ";pid=" + android.os.Process.myPid() + ";tid=" + android.os.Process.myTid() +
                    ";thread=android_main;world_state=externally_confirmed_only");
                setResultCode(result);
            }
        };
        registerReceiver(experimentalTrigger,
            new IntentFilter("dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE"),
            "android.permission.DUMP", null,
            Build.VERSION.SDK_INT >= 33 ? Context.RECEIVER_EXPORTED : 0);
        Log.i("HybridMCPE", "registry_trigger=registered;sender_permission=android.permission.DUMP;automatic=0");
    }
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
        if (getIntent().getBooleanExtra("experimental", false)) {
            Log.i("HybridMCPE", "host_stage=candidate_trace_armed elapsed_ns=" +
                android.os.SystemClock.elapsedRealtimeNanos() + " pid=" + android.os.Process.myPid() +
                " interception=disabled;engine_access=deferred_until_mod_start");
        }
        if (getIntent().getBooleanExtra("diagnostics", false)) {
            new Handler(Looper.getMainLooper()).post(() -> {
                if (destroyed) return;
                Log.i("HybridMCPE", "host_stage=diagnostic_begin pid=" + android.os.Process.myPid() +
                    " tid=" + android.os.Process.myTid() + " elapsed_ns=" + android.os.SystemClock.elapsedRealtimeNanos());
                String result = MainActivity.startDiagnostic(getApplicationInfo().nativeLibraryDir + "/libhybrid_diagnostic.so");
                diagnosticLoaded = result.endsWith(";diagnostic_load=0");
                diagnosticActive = diagnosticLoaded;
                Log.i("HybridMCPE", "host_stage=diagnostic_attempt pid=" + android.os.Process.myPid() + " result=" + result);
                if (diagnosticLoaded && getIntent().getBooleanExtra("experimental", false)) {
                    String experiment = MainActivity.startExperimentalRegistryProbe(
                        getApplicationInfo().nativeLibraryDir + "/libhybrid_experimental_registry_probe.so");
                    Log.i("HybridMCPE", "host_stage=experimental_registry_probe pid=" + android.os.Process.myPid() + " result=" + experiment);
                    if (experiment.equals("experimental_module_load=0")) registerExperimentalTrigger();
                }
            });
        }
    }
    @Override public void onPause() {
        resumed = false;
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
        resumed = true;
        if (diagnosticLoaded && !diagnosticActive) {
            int code = MainActivity.diagnosticLifecycle(true);
            diagnosticActive = code == 0;
            Log.i("HybridMCPE", "diagnostic_resume=" + code);
        }
        Log.i("HybridMCPE", "host_stage=resume pid=" + android.os.Process.myPid());
    }
    @Override public void onDestroy() {
        destroyed = true;
        if (experimentalTrigger != null) unregisterReceiver(experimentalTrigger);
        if (diagnosticLoaded) Log.i("HybridMCPE", "diagnostic_shutdown=" + MainActivity.stopDiagnostic());
        super.onDestroy();
        Log.i("HybridMCPE", "host_stage=destroy pid=" + android.os.Process.myPid());
    }
}
