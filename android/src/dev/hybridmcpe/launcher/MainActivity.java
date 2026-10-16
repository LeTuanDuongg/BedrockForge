package dev.hybridmcpe.launcher;

import android.app.*;
import android.os.Bundle;
import android.content.*;
import android.content.pm.*;
import android.net.Uri;
import android.widget.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public final class MainActivity extends Activity {
    static { System.loadLibrary("hybridmcpe"); }
    private static native String nativeProfile(String hash, long size, int machine);
    private static native String nativeDiagnostic(String path);
    private static native int nativeDiagnosticLifecycle(boolean active);
    private static native String nativeDiagnosticShutdown();
    public static String startDiagnostic(String path) { return nativeDiagnostic(path); }
    public static int diagnosticLifecycle(boolean active) { return nativeDiagnosticLifecycle(active); }
    public static String stopDiagnostic() { return nativeDiagnosticShutdown(); }
    private TextView status;
    private Spinner versions;
    private Button imported, installed, launch, diagnostic;
    private ApkRepository repository;
    private List<ApkRepository.Version> entries = new ArrayList<>();
    private boolean busy;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL); layout.setPadding(24, 32, 24, 24);
        TextView title = new TextView(this); title.setText("HybridMCPE"); title.setTextSize(26); layout.addView(title);
        installed = button(layout, "Dùng Minecraft đang cài", () -> work(() -> {
            ApplicationInfo app = getPackageManager().getApplicationInfo("com.mojang.minecraftpe", 0);
            List<File> apks = new ArrayList<>(); apks.add(new File(app.sourceDir));
            if (app.splitSourceDirs != null) for (String p : app.splitSourceDirs) apks.add(new File(p));
            ApkRepository.Version v = repository.importApks(apks); refresh(v.id());
        }));
        imported = button(layout, "Nhập APK / chọn toàn bộ split APK", () -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true); startActivityForResult(intent, 1);
        });
        versions = new Spinner(this); layout.addView(versions);
        versions.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            public void onItemSelected(android.widget.AdapterView<?> p, android.view.View v, int i, long id) { show(); }
            public void onNothingSelected(android.widget.AdapterView<?> p) {}
        });
        launch = button(layout, "Thử chạy engine trong HybridMCPE", () -> confirm(false));
        diagnostic = button(layout, "Thử host + diagnostic native mod", () -> confirm(true));
        status = new TextView(this); status.setTextIsSelectable(true); layout.addView(status);
        ScrollView scroll = new ScrollView(this); scroll.addView(layout); setContentView(scroll);
        try {
            repository = new ApkRepository(new File(getFilesDir(), "versions"), android.os.Build.VERSION.SDK_INT,
                (hash, size, machine) -> nativeProfile(hash, size, machine));
            work(() -> refresh(null));
        } catch (Exception e) { status.setText(e.toString()); }
    }
    private Button button(LinearLayout layout, String label, Runnable action) {
        Button b = new Button(this); b.setText(label); b.setOnClickListener(v -> action.run()); layout.addView(b); return b;
    }
    interface Job { void run() throws Exception; }
    private void work(Job job) {
        if (busy) return; busy = true; setBusy(true);
        new Thread(() -> {
            try { job.run(); } catch (Exception e) { android.util.Log.e("HybridMCPE", "import_or_host_failed", e); runOnUiThread(() -> status.setText("Lỗi: " + e)); }
            finally { runOnUiThread(() -> { busy = false; setBusy(false); }); }
        }, "HybridMCPE-import").start();
    }
    private void setBusy(boolean value) {
        imported.setEnabled(!value); installed.setEnabled(!value); versions.setEnabled(!value);
        launch.setEnabled(!value && !entries.isEmpty()); diagnostic.setEnabled(!value && !entries.isEmpty());
        if (value) status.setText("Đang sao chép và xác minh APK, chữ ký, engine…");
    }
    private void refresh(String selected) throws Exception {
        List<ApkRepository.Version> found = repository.list();
        runOnUiThread(() -> {
            entries = found; List<String> labels = new ArrayList<>(); int selection = 0;
            for (int i = 0; i < found.size(); ++i) {
                ApkRepository.Version v = found.get(i); labels.add(v.info.getProperty("version") + " • " + v.id().substring(0, 8));
                if (v.id().equals(selected)) selection = i;
            }
            versions.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, labels));
            versions.setSelection(selection); show();
        });
    }
    private ApkRepository.Version selected() { int i = versions.getSelectedItemPosition(); return i >= 0 && i < entries.size() ? entries.get(i) : null; }
    private void show() {
        ApkRepository.Version v = selected();
        if (v == null) { status.setText("Chọn Minecraft đang cài hoặc nhập APK. Không yêu cầu nguồn Google Play."); return; }
        Properties p = v.info;
        status.setText("Minecraft " + p.getProperty("version") + " (" + p.getProperty("versionCode") + ")\n" +
            p.getProperty("package") + "\nAPK/split: " + p.getProperty("apk.count") + "\nSigner SHA-256: " + p.getProperty("signers.sha256") +
            "\nEngine SHA-256: " + p.getProperty("engine.sha256") + "\nProfile: " + p.getProperty("engine.profile") +
            "\nGameplay capabilities: 0\nDữ liệu riêng: " + new File(v.directory, "data") +
            "\n\nNguồn/chủ thể phát hành chưa xác minh. Chữ ký hợp lệ không chứng minh bản chính thức hoặc quyền sử dụng.\n" +
            "Host thử nghiệm: chưa xác nhận vào menu, hỗ trợ tài khoản hoặc chơi mod. Kiểm tra giấy phép/integrity gốc được giữ nguyên.");
    }
    private void confirm(boolean diagnostics) {
        ApkRepository.Version v = selected(); if (v == null) return;
        new AlertDialog.Builder(this).setTitle(diagnostics ? "Đã vào menu bằng host này?" : "Thử engine nhập vào?")
            .setMessage((diagnostics ? "Chỉ thử diagnostic sau khi bản host không mod đã vào menu. " : "") +
                "Gói có nguồn chưa xác minh sẽ chạy mã trong sandbox HybridMCPE. Không đăng nhập tài khoản cá nhân hoặc mở world hiện có. Kiểm tra giấy phép gốc có thể từ chối host này.")
            .setNegativeButton("Hủy", null).setPositiveButton("Chạy thử", (d,w) -> startHost(v, diagnostics)).show();
    }
    private void startHost(ApkRepository.Version v, boolean diagnostics) {
        work(() -> {
            if (!"true".equals(v.info.getProperty("engine.runtimeSupported"))) throw new IOException("Engine chỉ có fingerprint; chưa có profile runtime được hỗ trợ");
            ActivityManager am = (ActivityManager)getSystemService(ACTIVITY_SERVICE);
            List<ActivityManager.RunningAppProcessInfo> ps = am.getRunningAppProcesses();
            if (ps != null) for (ActivityManager.RunningAppProcessInfo process : ps)
                if (process.uid == android.os.Process.myUid() && process.processName.endsWith(":game")) {
                    throw new IOException("Host vẫn đang chạy. Quay lại bằng màn đa nhiệm; đóng host trước khi đổi phiên bản/chế độ diagnostic");
                }
            repository.verify(v);
            File tmp = new File(getFilesDir(), "host-selection.tmp");
            try (DataOutputStream out = new DataOutputStream(new FileOutputStream(tmp))) { out.writeUTF(v.id()); }
            Files.move(tmp.toPath(), new File(getFilesDir(), "host-selection").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            runOnUiThread(() -> startActivity(new Intent().setClassName(this, "dev.hybridmcpe.launcher.HostedMinecraftActivity").putExtra("diagnostics", diagnostics)));
        });
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != 1 || result != RESULT_OK || data == null) return;
        List<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null) for (int i = 0; i < data.getClipData().getItemCount(); ++i) uris.add(data.getClipData().getItemAt(i).getUri());
        else if (data.getData() != null) uris.add(data.getData());
        work(() -> {
            File temp = Files.createTempDirectory(getCacheDir().toPath(), "apk-pick-").toFile();
            try {
                if (uris.isEmpty() || uris.size() > 128) throw new IOException("Chọn 1–128 APK");
                List<File> inputs = new ArrayList<>();
                for (int i = 0; i < uris.size(); ++i) {
                    File f = new File(temp, i + ".apk");
                    try (InputStream in = getContentResolver().openInputStream(uris.get(i))) {
                        if (in == null) throw new IOException("Không mở được tài liệu"); ApkRepository.copy(in, f, 4L << 30);
                    }
                    inputs.add(f);
                }
                ApkRepository.Version v = repository.importApks(inputs); refresh(v.id());
            } finally { ApkRepository.deleteTree(temp.toPath()); }
        });
    }
}
