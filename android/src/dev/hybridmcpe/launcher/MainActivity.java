package dev.hybridmcpe.launcher;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.widget.*;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.zip.ZipFile;

public final class MainActivity extends Activity {
    private static final String GAME = "com.mojang.minecraftpe";
    private static final String VERSION = "1.26.52.3";
    private static final String ENGINE_SHA256 = "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a";
    static { System.loadLibrary("hybridmcpe"); }
    private static native String nativeProbe();
    private TextView status;
    private Button open;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 48, 32, 32);
        TextView title = new TextView(this);
        title.setText("HybridMCPE — native runtime");
        title.setTextSize(24);
        layout.addView(title);
        status = new TextView(this);
        status.setText("Checking installed Minecraft engine…");
        status.setTextIsSelectable(true);
        layout.addView(status);
        open = new Button(this);
        open.setText("Open Minecraft (mods inactive)");
        open.setEnabled(false);
        open.setOnClickListener(v -> {
            Intent intent = getPackageManager().getLaunchIntentForPackage(GAME);
            if (intent != null) startActivity(intent);
            else status.setText("Minecraft launch activity unavailable.");
        });
        layout.addView(open);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
        new Thread(this::inspect, "HybridMCPE-engine-check").start();
    }

    private void inspect() {
        String report;
        boolean verified = false;
        try {
            PackageInfo info = getPackageManager().getPackageInfo(GAME, 0);
            ApplicationInfo app = info.applicationInfo;
            if (app == null) throw new IllegalStateException("Missing application metadata");
            String digest = null;
            String[] paths = app.splitSourceDirs == null ? new String[]{app.sourceDir} :
                java.util.stream.Stream.concat(java.util.stream.Stream.of(app.sourceDir),
                    java.util.Arrays.stream(app.splitSourceDirs)).toArray(String[]::new);
            for (String path : paths) {
                try (ZipFile apk = new ZipFile(path)) {
                    var entry = apk.getEntry("lib/arm64-v8a/libminecraftpe.so");
                    if (entry == null) continue;
                    if (digest != null) throw new IllegalStateException("Ambiguous engine libraries");
                    MessageDigest sha = MessageDigest.getInstance("SHA-256");
                    try (InputStream in = apk.getInputStream(entry)) {
                        byte[] buffer = new byte[65536];
                        int count;
                        while ((count = in.read(buffer)) != -1) sha.update(buffer, 0, count);
                    }
                    StringBuilder hex = new StringBuilder();
                    for (byte b : sha.digest()) hex.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
                    digest = hex.toString();
                }
            }
            verified = VERSION.equals(info.versionName) && ENGINE_SHA256.equals(digest);
            report = "Minecraft: " + info.versionName + "\nEngine SHA-256: " + digest +
                "\nTarget: " + (verified ? "verified" : "unsupported") +
                "\n\nLauncher process: " + nativeProbe() +
                "\n\nNative payload: " + getApplicationInfo().nativeLibraryDir +
                "\n\nActivation unavailable: Android isolates this launcher from Minecraft. " +
                "An in-process bootstrap is still required. Iron Chest gameplay bindings are unverified. " +
                "Opening Minecraft alone does not activate mods.";
        } catch (Exception e) {
            report = "Engine check failed: " + e + "\nNative runtime: " + nativeProbe();
        }
        String result = report;
        boolean allowOpen = verified;
        runOnUiThread(() -> { status.setText(result); open.setEnabled(allowOpen); });
    }
}
