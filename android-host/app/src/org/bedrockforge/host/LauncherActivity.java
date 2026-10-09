package org.bedrockforge.host;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.*;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.zip.ZipFile;

public final class LauncherActivity extends Activity {
 private TextView status;
 private static final String EXPECTED_VERSION="1.26.30.5";
 private static final String EXPECTED_ENGINE="433c79ce83e9862b45405b09a215e2e10f5dc9858b681c75f4ce3ea670236970";
 @Override public void onCreate(Bundle saved) {
  super.onCreate(saved);
  LinearLayout layout=new LinearLayout(this);layout.setOrientation(1);layout.setPadding(24,24,24,24);
  status=new TextView(this);status.setText("BedrockForge system prototype. Mod discovery/import chưa tích hợp. Game dùng profile riêng.");
  Button launch=new Button(this);launch.setText("Khởi chạy Minecraft PE đã ghim");
  layout.addView(status);layout.addView(launch);setContentView(layout);
  launch.setOnClickListener(v -> {launch.setEnabled(false);status.setText("Đang chuẩn bị engine…");new Thread(() -> {
   try {prepare();runOnUiThread(() -> {startActivity(new Intent().setClassName(this,"com.mojang.minecraftpe.MainActivity"));launch.setEnabled(true);});}
   catch(Exception error) {android.util.Log.e("BedrockForge", "Preparation failed",error);runOnUiThread(() -> {status.setText(error.toString());launch.setEnabled(true);});}
  }).start();});
  if(getIntent().getBooleanExtra("test_launch",false))launch.performClick();
 }
 private void prepare() throws Exception {
  PackageInfo pkg=getPackageManager().getPackageInfo("com.mojang.minecraftpe",0);
  if(!EXPECTED_VERSION.equals(pkg.versionName)) throw new IOException("Chưa hỗ trợ bản "+pkg.versionName);
  File root=new File(getFilesDir().getParentFile(),"game-runtime"),lib=new File(root,"lib");
  if(!lib.isDirectory()&&!lib.mkdirs())throw new IOException("Cannot create runtime directory");
  try(ZipFile apk=new ZipFile(pkg.applicationInfo.sourceDir)) {
   for(var entry:java.util.Collections.list(apk.entries())) {
    if(!entry.getName().startsWith("lib/arm64-v8a/")||!entry.getName().endsWith(".so"))continue;
    File out=new File(lib,new File(entry.getName()).getName());
    if(out.length()!=entry.getSize()) {
     File temp=new File(lib,out.getName()+".tmp");
     try(InputStream in=apk.getInputStream(entry);OutputStream dst=new FileOutputStream(temp)){byte[] buffer=new byte[65536];int size;while((size=in.read(buffer))>0)dst.write(buffer,0,size);}
     Files.move(temp.toPath(),out.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);out.setReadOnly();
    }
   }
  }
  MessageDigest hash=MessageDigest.getInstance("SHA-256");
  try(InputStream in=new FileInputStream(new File(lib,"libminecraftpe.so"))){byte[] b=new byte[65536];int n;while((n=in.read(b))>0)hash.update(b,0,n);}
  StringBuilder hex=new StringBuilder();for(byte b:hash.digest())hex.append(String.format("%02x",b&255));
  if(!EXPECTED_ENGINE.equals(hex.toString()))throw new IOException("Engine hash chưa được kiểm tra");
  Files.write(new File(root,"apk-path").toPath(),pkg.applicationInfo.sourceDir.getBytes(java.nio.charset.StandardCharsets.UTF_8));
 }
}

