package dev.hybridmcpe.launcher;

import com.android.apksig.ApkVerifier;
import com.android.apksig.internal.apk.AndroidBinXmlParser;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.*;
import java.security.MessageDigest;
import java.security.cert.X509Certificate;
import java.util.*;
import java.util.zip.*;

/** Immutable APK imports. No imported Java or native code is executed here. */
public final class ApkRepository {
    public interface Profiles { String find(String hash, long size, int machine); }
    private static final long APK_LIMIT = 4L << 30;
    private static final long LIB_LIMIT = 2L << 30;
    private final File root;
    private final int api;
    private final Profiles profiles;

    public ApkRepository(File root, int api, Profiles profiles) throws IOException {
        this.root = root.getCanonicalFile();
        this.api = api;
        this.profiles = profiles;
        Files.createDirectories(this.root.toPath());
    }

    public static final class Version {
        public final File directory;
        public final Properties info;
        Version(File directory, Properties info) { this.directory = directory; this.info = info; }
        public String id() { return directory.getName(); }
        public File[] apks() {
            int count = Integer.parseInt(info.getProperty("apk.count"));
            File[] files = new File[count];
            for (int i = 0; i < count; ++i) files[i] = new File(directory, "apks/" + i + ".apk");
            return files;
        }
    }

    public List<Version> list() throws IOException {
        List<Version> result = new ArrayList<>();
        File[] dirs = root.listFiles();
        if (dirs == null) throw new IOException("Cannot list version repository");
        for (File dir : dirs) {
            if (!dir.getName().matches("[a-f0-9]{64}")) continue;
            Properties p = new Properties();
            try (InputStream in = new FileInputStream(new File(dir, "version.properties"))) { p.load(in); }
            result.add(new Version(dir, p));
        }
        result.sort(Comparator.comparing(Version::id));
        return result;
    }

    public Version importApks(List<File> inputs) throws Exception {
        if (inputs.isEmpty() || inputs.size() > 128) throw new IOException("Select 1–128 APK files, including base and required splits");
        File stage = Files.createTempDirectory(root.toPath(), ".import-").toFile();
        try {
            File apkDir = new File(stage, "apks");
            Files.createDirectories(apkDir.toPath());
            List<Archive> archives = new ArrayList<>();
            long total = 0;
            for (int i = 0; i < inputs.size(); ++i) {
                File copy = new File(stage, "source-" + i + ".apk");
                try (InputStream in = new FileInputStream(inputs.get(i))) { total += copy(in, copy, APK_LIMIT); }
                if (total > (8L << 30)) throw new IOException("APK set exceeds 8 GiB");
                archives.add(inspect(copy));
            }
            archives.sort(Comparator.comparing(a -> a.split));
            Archive base = archives.get(0);
            if (!base.split.isEmpty()) throw new IOException("Missing base APK");
            Set<String> splits = new HashSet<>(), types = new HashSet<>();
            for (Archive a : archives) {
                if (!splits.add(a.split)) throw new IOException("Duplicate base/split: " + a.split);
                if (!base.pkg.equals(a.pkg) || base.code != a.code || !base.signers.equals(a.signers))
                    throw new IOException("APK set has mixed package, versionCode or signer identity");
                types.addAll(a.splitTypes);
            }
            if (base.splitRequired && archives.size() == 1) throw new IOException("Base APK requires splits");
            for (Archive a : archives) {
                if (!splits.containsAll(a.dependencies) || !types.containsAll(a.requiredTypes))
                    throw new IOException("Missing required split for " + a.split);
            }
            MessageDigest identity = MessageDigest.getInstance("SHA-256");
            Properties p = new Properties();
            p.setProperty("schema", "1");
            p.setProperty("package", base.pkg);
            p.setProperty("version", base.version);
            p.setProperty("versionCode", Long.toUnsignedString(base.code));
            p.setProperty("application", base.application);
            p.setProperty("mainActivity", base.mainActivity);
            p.setProperty("theme", Integer.toUnsignedString(base.theme));
            p.setProperty("minSdk", Integer.toString(base.minSdk));
            p.setProperty("signers.sha256", String.join(",", base.signers));
            p.setProperty("signers.subject", base.subjects);
            p.setProperty("signature.verifiedForAndroidApi", Integer.toString(api));
            p.setProperty("publisher", "unknown; signature validity does not establish official publication or a license");
            p.setProperty("apk.count", Integer.toString(archives.size()));
            p.setProperty("split.completeness", "declared dependencies satisfied; runtime completeness unverified");
            File nativeDir = new File(stage, "native/arm64-v8a");
            Files.createDirectories(nativeDir.toPath());
            Set<String> libraries = new TreeSet<>();
            long extracted = 0;
            for (int i = 0; i < archives.size(); ++i) {
                Archive a = archives.get(i);
                identity.update(a.hash.getBytes(java.nio.charset.StandardCharsets.US_ASCII));
                File apk = new File(apkDir, i + ".apk");
                Files.move(a.file.toPath(), apk.toPath());
                p.setProperty("apk." + i + ".sha256", a.hash);
                p.setProperty("apk." + i + ".split", a.split);
                p.setProperty("apk." + i + ".signature", a.schemes);
                try (ZipFile zip = new ZipFile(apk)) {
                    Set<String> entries = new HashSet<>();
                    Enumeration<? extends ZipEntry> es = zip.entries();
                    while (es.hasMoreElements()) {
                        ZipEntry e = es.nextElement();
                        if (!entries.add(e.getName())) throw new IOException("Duplicate ZIP entry");
                        if (!e.getName().startsWith("lib/arm64-v8a/") || e.isDirectory()) continue;
                        String name = e.getName().substring("lib/arm64-v8a/".length());
                        if (!name.matches("[A-Za-z0-9_.+-]+\\.so") || !libraries.add(name))
                            throw new IOException("Unsafe or duplicate ARM64 library: " + name);
                        File dest = new File(nativeDir, name);
                        try (InputStream in = zip.getInputStream(e)) { extracted += copy(in, dest, LIB_LIMIT); }
                        if (extracted > (3L << 30)) throw new IOException("Native payload exceeds 3 GiB");
                        requireArm64(dest);
                        p.setProperty("library." + name + ".sha256", sha(dest));
                        readonly(dest);
                    }
                }
                readonly(apk);
            }
            File engine = new File(nativeDir, "libminecraftpe.so");
            if (!engine.isFile()) throw new IOException("ARM64 libminecraftpe.so missing; select the ARM64 split too");
            String hash = sha(engine);
            String profile = profiles.find(hash, engine.length(), 183);
            p.setProperty("engine.sha256", hash);
            p.setProperty("engine.size", Long.toString(engine.length()));
            p.setProperty("engine.architecture", "AArch64");
            p.setProperty("engine.profile", profile == null || profile.isEmpty() ? "fingerprint-only:" + hash : profile);
            p.setProperty("engine.runtimeSupported", Boolean.toString(profile != null && !profile.isEmpty()));
            p.setProperty("engine.capabilities", "0");
            p.setProperty("engine.missingContracts", "gameplay bridge; container; registry; UI; persistence");
            p.setProperty("libraries", String.join(",", libraries));
            p.setProperty("execution", "experimental host; unvalidated startup, licensing and native lifecycle");
            Files.createDirectories(new File(stage, "data").toPath());
            try (OutputStream out = new FileOutputStream(new File(stage, "version.properties"))) { p.store(out, "HybridMCPE imported APK set — no execution claim"); }
            String json = "{\n  \"sha256\": \"" + hash + "\",\n  \"size\": " + engine.length() +
                ",\n  \"architecture\": \"AArch64\",\n  \"capabilities\": 0,\n  \"layouts\": [],\n  \"callable_contracts\": [],\n  \"status\": \"fingerprint-only; runtime support requires exact built-in profile\"\n}\n";
            Files.write(new File(stage, "engine-profile.json").toPath(), json.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            File finalDir = new File(root, hex(identity.digest()));
            if (finalDir.exists()) {
                for (Version existing : list()) if (existing.directory.equals(finalDir)) { verify(existing); return existing; }
                throw new IOException("Incomplete existing version directory");
            }
            Files.move(stage.toPath(), finalDir.toPath(), StandardCopyOption.ATOMIC_MOVE);
            return new Version(finalDir, p);
        } finally { deleteTree(stage.toPath()); }
    }

    /** Recheck immutable bytes immediately before any dynamic class loading. */
    public void verify(Version v) throws Exception {
        if (!v.directory.getCanonicalFile().getParentFile().equals(root) || !v.id().matches("[a-f0-9]{64}"))
            throw new IOException("Invalid version path");
        File[] apks = v.apks();
        if (apks.length < 1 || apks.length > 128) throw new IOException("Invalid APK count");
        for (int i = 0; i < apks.length; ++i)
            if (!sha(apks[i]).equals(v.info.getProperty("apk." + i + ".sha256"))) throw new IOException("Imported APK changed");
        for (String name : v.info.getProperty("libraries").split(",")) {
            if (!name.matches("[A-Za-z0-9_.+-]+\\.so")) throw new IOException("Invalid library path");
            File lib = new File(v.directory, "native/arm64-v8a/" + name);
            if (!sha(lib).equals(v.info.getProperty("library." + name + ".sha256"))) throw new IOException("Imported library changed: " + name);
        }
    }

    private Archive inspect(File apk) throws Exception {
        ApkVerifier.Result r = new ApkVerifier.Builder(apk).setMinCheckedPlatformVersion(api).setMaxCheckedPlatformVersion(api).build().verify();
        if (!r.isVerified() || r.getSignerCertificates().isEmpty()) throw new IOException("APK signature verification failed: " + r.getErrors());
        Archive a = new Archive(); a.file = apk; a.hash = sha(apk);
        for (X509Certificate cert : r.getSignerCertificates()) {
            a.signers.add(hex(MessageDigest.getInstance("SHA-256").digest(cert.getEncoded())));
            a.subjects += cert.getSubjectX500Principal().getName() + "\n";
        }
        a.schemes = "v1=" + r.isVerifiedUsingV1Scheme() + ";v2=" + r.isVerifiedUsingV2Scheme() + ";v3=" + r.isVerifiedUsingV3Scheme();
        try (ZipFile zip = new ZipFile(apk)) {
            ZipEntry entry = zip.getEntry("AndroidManifest.xml");
            if (entry == null) throw new IOException("Missing AndroidManifest.xml");
            byte[] bytes;
            try (InputStream in = zip.getInputStream(entry)) { bytes = bounded(in, 4 << 20); }
            // SDK 36.0.0 parser is pinned with the verifier; do not silently change its internal API.
            AndroidBinXmlParser parser = new AndroidBinXmlParser(ByteBuffer.wrap(bytes));
            int code = 0, major = 0;
            while (parser.next() != AndroidBinXmlParser.EVENT_END_DOCUMENT) {
                if (parser.getEventType() != AndroidBinXmlParser.EVENT_START_ELEMENT) continue;
                String tag = parser.getName();
                for (int i = 0; i < parser.getAttributeCount(); ++i) {
                    String name = parser.getAttributeName(i), ns = parser.getAttributeNamespace(i);
                    boolean android = "http://schemas.android.com/apk/res/android".equals(ns);
                    int type = parser.getAttributeValueType(i);
                    String value = type == AndroidBinXmlParser.VALUE_TYPE_STRING ? parser.getAttributeStringValue(i) : "";
                    if ("manifest".equals(tag)) {
                        if (!android && "package".equals(name)) a.pkg = value;
                        if (!android && "split".equals(name)) a.split = value;
                        if (!android && "configForSplit".equals(name) && !value.isEmpty()) a.dependencies.add(value);
                        if (android && "versionName".equals(name)) a.version = value;
                        if (android && "versionCode".equals(name)) code = parser.getAttributeIntValue(i);
                        if (android && "versionCodeMajor".equals(name)) major = parser.getAttributeIntValue(i);
                        if (android && "splitTypes".equals(name)) addTypes(a.splitTypes, value);
                        if (android && "requiredSplitTypes".equals(name)) addTypes(a.requiredTypes, value);
                    }
                    if ("uses-sdk".equals(tag) && android && "minSdkVersion".equals(name)) a.minSdk = parser.getAttributeIntValue(i);
                    if ("uses-split".equals(tag) && android && "name".equals(name)) a.dependencies.add(value);
                    if ("application".equals(tag) && android && "name".equals(name)) a.application = value;
                    if ("application".equals(tag) && android && "isSplitRequired".equals(name)) a.splitRequired = parser.getAttributeBooleanValue(i);
                    if ("activity".equals(tag) && android && "name".equals(name) && value.equals("com.mojang.minecraftpe.MainActivity")) a.mainActivity = value;
                    if ("activity".equals(tag) && android && "theme".equals(name) && a.theme == 0) a.theme = parser.getAttributeIntValue(i);
                }
            }
            a.code = ((long)major << 32) | Integer.toUnsignedLong(code);
        }
        if (a.pkg.isEmpty() || a.minSdk > api) throw new IOException("Missing package or unsupported Android minSdk " + a.minSdk);
        return a;
    }

    private static class Archive {
        File file; String hash, pkg = "", version = "", split = "", subjects = "", schemes;
        String application = "android.app.Application", mainActivity = "";
        long code; int minSdk = 1, theme; boolean splitRequired;
        Set<String> signers = new TreeSet<>(), dependencies = new HashSet<>(), splitTypes = new HashSet<>(), requiredTypes = new HashSet<>();
    }
    private static void addTypes(Set<String> out, String value) { for (String s : value.split(",")) if (!s.trim().isEmpty()) out.add(s.trim()); }
    static void requireArm64(File f) throws IOException {
        byte[] b = new byte[20];
        try (DataInputStream in = new DataInputStream(new FileInputStream(f))) { in.readFully(b); }
        if (b[0] != 127 || b[1] != 'E' || b[2] != 'L' || b[3] != 'F' || b[4] != 2 || b[5] != 1 ||
            ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getShort(18) != 183) throw new IOException("Not an AArch64 ELF: " + f.getName());
    }
    public static String sha(File f) throws Exception {
        MessageDigest d = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) { byte[] b = new byte[65536]; int n; while ((n = in.read(b)) != -1) d.update(b, 0, n); }
        return hex(d.digest());
    }
    private static String hex(byte[] bytes) { StringBuilder s = new StringBuilder(); for (byte b : bytes) s.append(String.format(Locale.ROOT, "%02x", b & 255)); return s.toString(); }
    static long copy(InputStream in, File dest, long limit) throws IOException {
        try (OutputStream out = new FileOutputStream(dest)) {
            byte[] b = new byte[65536]; long total = 0; int n;
            while ((n = in.read(b)) != -1) { total += n; if (total > limit) throw new IOException("Import size limit exceeded"); out.write(b, 0, n); }
            return total;
        }
    }
    private static byte[] bounded(InputStream in, int max) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(); byte[] b = new byte[8192]; int n;
        while ((n = in.read(b)) != -1) { if (out.size() + n > max) throw new IOException("Manifest too large"); out.write(b, 0, n); }
        return out.toByteArray();
    }
    private static void readonly(File f) throws IOException { if (!f.setReadOnly()) throw new IOException("Cannot make imported code read-only"); }
    static void deleteTree(Path path) throws IOException {
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS)) return;
        try (java.util.stream.Stream<Path> paths = Files.walk(path)) {
            for (Path p : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) { if (!Files.isDirectory(p)) p.toFile().setWritable(true); Files.delete(p); }
        }
    }
}
