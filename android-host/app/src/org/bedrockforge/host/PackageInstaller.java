package org.bedrockforge.host;

import android.content.Context;
import android.net.Uri;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Validates and installs flat, signed-by-hash BedrockForge native packages. */
final class PackageInstaller {
 private static final int MAX_ARCHIVE=128*1024*1024;
 private static final String GAME_VERSION="1.26.30.5";
 private static final Pattern ID=Pattern.compile("[a-z][a-z0-9_]{1,63}");
 private PackageInstaller(){}

 static String install(Context context,Uri uri) throws Exception {
  Map<String,byte[]> files=readArchive(context,uri);
  byte[] rawManifest=required(files,"bedrockforge.mod.json");
  JSONObject manifest=new JSONObject(new String(rawManifest,StandardCharsets.UTF_8));
  validateManifest(manifest);
  verifyChecksums(files,new JSONObject(new String(required(files,"SHA256SUMS.json"),StandardCharsets.UTF_8)));
  String id=manifest.getString("id");
  validateDependencies(context,id,manifest.getJSONObject("dependencies"));
  JSONObject nativeInfo=manifest.getJSONObject("native");
  String library=nativeInfo.getString("library");
  if(files.size()!=3||!files.containsKey("SHA256SUMS.json")||!files.containsKey("bedrockforge.mod.json")||!files.containsKey(library))throw new java.io.IOException("Unexpected package contents");
  byte[] binary=required(files,library);
  validateElf(binary);

  File root=new File(context.getFilesDir(),"mods");
  File packages=new File(root,"packages"),enabled=new File(root,"enabled");
  if(!packages.isDirectory()&&!packages.mkdirs())throw new java.io.IOException("Cannot create package directory");
  if(!enabled.isDirectory()&&!enabled.mkdirs())throw new java.io.IOException("Cannot create enabled directory");
  File installed=new File(packages,id),active=new File(enabled,"lib"+id+".so");
  if(installed.exists()||active.exists())throw new java.io.IOException("Mod ID already installed: "+id);
  File stage=new File(packages,id+".installing");
  deleteTree(stage);
  if(!stage.mkdirs())throw new java.io.IOException("Cannot create staging directory");
  try {
   writeSync(new File(stage,"manifest.json"),rawManifest);
   writeSync(new File(stage,library),binary);
   File activeTemp=new File(enabled,"lib"+id+".so.installing");
   writeSync(activeTemp,binary);
   Files.move(stage.toPath(),installed.toPath(),StandardCopyOption.ATOMIC_MOVE);
   try { Files.move(activeTemp.toPath(),active.toPath(),StandardCopyOption.ATOMIC_MOVE); }
   catch(Exception failure){deleteTree(installed);activeTemp.delete();throw failure;}
  } catch(Exception failure){deleteTree(stage);throw failure;}
  return manifest.getString("display_name")+" ("+id+") installed. Native mods run with Minecraft process privileges.";
 }

 private static Map<String,byte[]> readArchive(Context context,Uri uri) throws Exception {
  Map<String,byte[]> out=new HashMap<>();int total=0;
  try(InputStream source=context.getContentResolver().openInputStream(uri)){
   if(source==null)throw new java.io.IOException("Cannot open selected package");
   try(ZipInputStream zip=new ZipInputStream(source)){
    ZipEntry entry;
    while((entry=zip.getNextEntry())!=null){
     String name=entry.getName();
     if(entry.isDirectory()||name.isEmpty()||name.contains("/")||name.contains("\\")||name.equals(".")||name.equals(".."))throw new java.io.IOException("Package entries must be flat files");
     ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] buffer=new byte[16384];int n;
     while((n=zip.read(buffer))!=-1){total+=n;if(total>MAX_ARCHIVE)throw new java.io.IOException("Package exceeds 128 MiB");bytes.write(buffer,0,n);}
     if(out.put(name,bytes.toByteArray())!=null)throw new java.io.IOException("Duplicate package entry: "+name);
     zip.closeEntry();
    }
   }
  }
  if(out.size()!=3)throw new java.io.IOException("Unsupported package entry count");
  return out;
 }

 private static void validateManifest(JSONObject m) throws Exception {
  Set<String> expected=new HashSet<>();java.util.Collections.addAll(expected,"id","display_name","author","version","framework_api","minecraft_versions","dependencies","optional_dependencies","capabilities","native");
  Set<String> actual=new HashSet<>();for(Iterator<String> it=m.keys();it.hasNext();)actual.add(it.next());
  if(!actual.equals(expected))throw new java.io.IOException("Unsupported manifest fields");
  String id=m.getString("id");if(!ID.matcher(id).matches())throw new java.io.IOException("Invalid mod ID");
  for(String field:new String[]{"display_name","author"}){String value=m.getString(field);if(value.isEmpty()||value.length()>128)throw new java.io.IOException("Invalid "+field);}
  semver(m.getString("version"));
  if(!"1.0.0".equals(m.getString("framework_api")))throw new java.io.IOException("Unsupported framework API");
  org.json.JSONArray versions=m.getJSONArray("minecraft_versions");boolean match=false;
  for(int i=0;i<versions.length();i++){String v=versions.getString(i);if(!v.matches("\\d+\\.\\d+\\.\\d+(\\.\\d+)?"))throw new java.io.IOException("Invalid Minecraft version");match|=GAME_VERSION.equals(v);}
  if(!match)throw new java.io.IOException("Package does not support pinned Minecraft "+GAME_VERSION);
  JSONObject n=m.getJSONObject("native");Set<String> nativeKeys=new HashSet<>();for(Iterator<String> it=n.keys();it.hasNext();)nativeKeys.add(it.next());
  if(!nativeKeys.equals(new HashSet<>(java.util.Arrays.asList("library","entry_point","abi","mod_api")))||!"bf_mod_entry".equals(n.getString("entry_point"))||!"arm64-v8a".equals(n.getString("abi"))||n.getInt("mod_api")!=2||!n.getString("library").matches("lib[a-z0-9_]+\\.so"))throw new java.io.IOException("Unsupported native module ABI");
  JSONObject required=m.getJSONObject("dependencies"),optional=m.getJSONObject("optional_dependencies");
  validateDependencyMap(required);validateDependencyMap(optional);
  for(Iterator<String> it=required.keys();it.hasNext();)if(optional.has(it.next()))throw new java.io.IOException("Dependency is declared twice");
  org.json.JSONArray capabilities=m.getJSONArray("capabilities");
  for(int i=0;i<capabilities.length();i++)if(!capabilities.getString(i).matches("[a-z0-9_.]+"))throw new java.io.IOException("Invalid capability name");
 }
 private static void validateDependencyMap(JSONObject deps) throws Exception {
  for(Iterator<String> it=deps.keys();it.hasNext();){String id=it.next();if(!ID.matcher(id).matches())throw new java.io.IOException("Invalid dependency ID");String range=deps.getString(id);if(range.startsWith(">="))semver(range.substring(2));else if(range.startsWith("=="))semver(range.substring(2));else semver(range);}
 }
 private static void validateDependencies(Context context,String installing,JSONObject required) throws Exception {
  File dir=new File(context.getFilesDir(),"mods/packages");
  for(Iterator<String> it=required.keys();it.hasNext();){String id=it.next();if(id.equals(installing))throw new java.io.IOException("A mod cannot depend on itself");File manifestFile=new File(new File(dir,id),"manifest.json");
   if(!manifestFile.isFile())throw new java.io.IOException("Install required dependency first: "+id);
   JSONObject dep=new JSONObject(new String(Files.readAllBytes(manifestFile.toPath()),StandardCharsets.UTF_8));
   if(!satisfies(dep.getString("version"),required.getString(id)))throw new java.io.IOException("Installed dependency version does not satisfy "+id);
  }
 }
 private static boolean satisfies(String version,String range) throws Exception {
  int[] v=semver(version);String op="";String wanted=range;
  if(range.startsWith(">=")){op=">=";wanted=range.substring(2);}else if(range.startsWith("==")){op="==";wanted=range.substring(2);}
  int[] r=semver(wanted);for(int i=0;i<3;i++)if(v[i]!=r[i])return op.equals(">=")&&v[i]>r[i];
  return true;
 }
 private static int[] semver(String value) throws Exception {
  if(!value.matches("(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)"))throw new java.io.IOException("Only numeric release semver is supported");
  String[] p=value.split("\\.");return new int[]{Integer.parseInt(p[0]),Integer.parseInt(p[1]),Integer.parseInt(p[2])};
 }
 private static void verifyChecksums(Map<String,byte[]> files,JSONObject sums) throws Exception {
  if(sums.length()!=files.size()-1)throw new java.io.IOException("Checksum manifest does not cover package entries");
  for(Map.Entry<String,byte[]> entry:files.entrySet())if(!entry.getKey().equals("SHA256SUMS.json")){
   String expected=sums.optString(entry.getKey(),"");String actual=hex(MessageDigest.getInstance("SHA-256").digest(entry.getValue()));
   if(!actual.equals(expected))throw new java.io.IOException("Checksum mismatch: "+entry.getKey());
  }
  for(Iterator<String> it=sums.keys();it.hasNext();)if(!files.containsKey(it.next()))throw new java.io.IOException("Checksum references absent file");
 }
 private static void validateElf(byte[] data) throws Exception {
  if(data.length<64||data[0]!=0x7f||data[1]!='E'||data[2]!='L'||data[3]!='F'||data[4]!=2||data[5]!=1||(data[16]&255)!=3||(data[17]&255)!=0||(data[18]&255)!=183||(data[19]&255)!=0)throw new java.io.IOException("Expected ARM64 little-endian ELF shared library");
 }
 private static byte[] required(Map<String,byte[]> files,String name) throws Exception {byte[] b=files.get(name);if(b==null)throw new java.io.IOException("Missing package entry: "+name);return b;}
 private static String hex(byte[] data){StringBuilder s=new StringBuilder();for(byte b:data)s.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return s.toString();}
 private static void writeSync(File file,byte[] data) throws Exception {
  try(FileOutputStream out=new FileOutputStream(file)){out.write(data);out.flush();out.getFD().sync();}
 }
 private static void deleteTree(File file){if(file.isDirectory()){File[] children=file.listFiles();if(children!=null)for(File child:children)deleteTree(child);}file.delete();}
}
