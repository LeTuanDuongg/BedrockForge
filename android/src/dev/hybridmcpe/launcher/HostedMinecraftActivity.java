package dev.hybridmcpe.launcher;

import android.content.Context;
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Calls the real imported MainActivity lifecycle, with isolated host storage. */
public final class HostedMinecraftActivity extends com.mojang.minecraftpe.MainActivity {
    private boolean diagnosticLoaded;
    private volatile boolean diagnosticActive;
    private volatile boolean destroyed;
    private volatile boolean resumed;
    private BroadcastReceiver experimentalTrigger;
    private LocalServerSocket adbTriggerSocket;
    private Thread adbTriggerThread;
    private void startAdbTriggerSocket() {
        try {
            adbTriggerSocket = new LocalServerSocket("hybridmcpe-button-diag");
        } catch (IOException e) {
            Log.e("HybridMCPE", "button_adb_socket=bind_failed", e);
            return;
        }
        adbTriggerThread = new Thread(() -> {
            while (!destroyed) {
                try (LocalSocket socket = adbTriggerSocket.accept()) {
                    socket.setSoTimeout(1000);
                    int uid = socket.getPeerCredentials().getUid();
                    int command = socket.getInputStream().read();
                    int operation = command == 'O' ? 0 : command == 'E' ? 1 : command == 'R' ? 2 : -1;
                    int result = uid == 2000 && operation >= 0 && resumed && diagnosticActive ?
                        MainActivity.triggerExperimentalButton(operation) : -1;
                    socket.getOutputStream().write(Integer.toString(result).getBytes(StandardCharsets.US_ASCII));
                    socket.getOutputStream().flush();
                    Log.i("HybridMCPE", "button_adb_command=complete;uid=" + uid + ";operation=" + operation +
                        ";result=" + result + ";pid=" + android.os.Process.myPid() +
                        ";elapsed_ns=" + android.os.SystemClock.elapsedRealtimeNanos());
                } catch (IOException e) {
                    if (!destroyed) Log.w("HybridMCPE", "button_adb_command=io_error", e);
                }
            }
        }, "HybridMCPE-ADB-diagnostic");
        adbTriggerThread.setDaemon(true);
        adbTriggerThread.start();
        Log.i("HybridMCPE", "button_adb_socket=ready;name=hybridmcpe-button-diag;accepted_uid=2000");
    }
    private void registerExperimentalTrigger() {
        if (!MainActivity.experimentalEnabled() || !getIntent().getBooleanExtra("experimental", false) ||
                !"dev.hybridmcpe.launcher".equals(getPackageName())) return;
        experimentalTrigger = new BroadcastReceiver() {
            @Override public void onReceive(Context context, Intent intent) {
                String action = intent.getAction();
                int operation = "dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_OBSERVE".equals(action) ? 0 :
                    "dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_EXTEND".equals(action) ? 1 :
                    "dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_RESET".equals(action) ? 2 : -1;
                if (operation >= 0) {
                    int result = !destroyed && resumed && diagnosticActive ?
                        MainActivity.triggerExperimentalButton(operation) : -1;
                    Log.i("HybridMCPE", "button_command=request;operation=" + operation + ";result=" + result +
                        ";pid=" + android.os.Process.myPid() + ";elapsed_ns=" + android.os.SystemClock.elapsedRealtimeNanos());
                    setResultCode(result);
                    return;
                }
                if (!"dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE".equals(action)) return;
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
        IntentFilter filter = new IntentFilter("dev.hybridmcpe.launcher.EXPERIMENTAL_REGISTRY_PROBE");
        filter.addAction("dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_OBSERVE");
        filter.addAction("dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_EXTEND");
        filter.addAction("dev.hybridmcpe.launcher.EXPERIMENTAL_BUTTON_RESET");
        registerReceiver(experimentalTrigger, filter,
            "android.permission.DUMP", null,
            Build.VERSION.SDK_INT >= 33 ? Context.RECEIVER_EXPORTED : 0);
        startAdbTriggerSocket();
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
                    if (experiment.equals("experimental_module_load=0")) {
                        String buttonMod = MainActivity.startExperimentalButtonMod(
                            getApplicationInfo().nativeLibraryDir + "/libhybrid_button_duration_mod.so");
                        Log.i("HybridMCPE", "host_stage=button_duration_mod pid=" + android.os.Process.myPid() + " result=" + buttonMod);
                        if (buttonMod.equals("button_mod_load=0")) registerExperimentalTrigger();
                    }
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
        if (adbTriggerSocket != null) {
            try { adbTriggerSocket.close(); } catch (IOException ignored) { }
        }
        if (adbTriggerThread != null) {
            try { adbTriggerThread.join(500); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        }
        if (diagnosticLoaded) Log.i("HybridMCPE", "diagnostic_shutdown=" + MainActivity.stopDiagnostic());
        super.onDestroy();
        Log.i("HybridMCPE", "host_stage=destroy pid=" + android.os.Process.myPid());
    }
}
