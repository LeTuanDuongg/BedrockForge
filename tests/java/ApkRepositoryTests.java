package dev.hybridmcpe.launcher;

import java.io.*;
import java.nio.file.*;
import java.util.*;

/** Static APK import tests. Never executes imported DEX or native libraries. */
public final class ApkRepositoryTests {
    static void check(boolean ok, String label) { if (!ok) throw new AssertionError(label); }
    interface Action { void run() throws Exception; }
    static void rejects(Action action, String label) throws Exception {
        try { action.run(); } catch (IOException expected) { return; }
        throw new AssertionError("Accepted " + label);
    }
    public static void main(String[] args) throws Exception {
        File source = new File(args[0]);
        String original = ApkRepository.sha(source);
        Path temp = Files.createTempDirectory("hybrid-apk-import-tests-");
        try {
            ApkRepository repo = new ApkRepository(temp.toFile(), 31, (hash, size, machine) ->
                hash.equals("fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1") &&
                size == 328419680 && machine == 183 ? "bedrock-1.26.50.4-arm64" : "");
            ApkRepository.Version v = repo.importApks(Arrays.asList(source));
            check(v.info.getProperty("version").equals("1.26.50.4"), "version");
            check(v.info.getProperty("versionCode").equals("972605004"), "versionCode");
            check(v.info.getProperty("engine.runtimeSupported").equals("true"), "exact profile");
            check(v.info.getProperty("engine.capabilities").equals("0"), "no gameplay");
            check(v.info.getProperty("signers.sha256").equals("b369922ee3e720f359b9eec13af554dc87a0ec09f3ac49e7e123bc94fb985890"), "signer");
            check(v.info.getProperty("mainActivity").equals("com.mojang.minecraftpe.MainActivity"), "real main activity");
            check(v.info.getProperty("application").equals("android.app.Application"), "application lifecycle");
            repo.verify(v);
            Files.write(new File(v.directory, "data/retain.txt").toPath(), new byte[]{42});
            check(repo.importApks(Arrays.asList(source)).id().equals(v.id()), "duplicate import identity");
            check(new File(v.directory, "data/retain.txt").isFile(), "duplicate import preserves data");
            rejects(() -> repo.importApks(Arrays.asList(source, source)), "duplicate base");
            check(repo.list().size() == 1, "failed import rollback");
            File lib = new File(v.directory, "native/arm64-v8a/libminecraftpe.so");
            check(lib.setWritable(true), "make test copy writable");
            try (RandomAccessFile f = new RandomAccessFile(lib, "rw")) { f.seek(64); f.writeByte(0x55); }
            rejects(() -> repo.verify(v), "changed extracted code");
            File invalid = new File(temp.toFile(), "not-an-apk"); Files.write(invalid.toPath(), new byte[]{1,2,3});
            boolean rejected = false;
            try { repo.importApks(Arrays.asList(invalid)); } catch (Exception expected) { rejected = true; }
            check(rejected, "unsigned/invalid APK rejected");
            check(original.equals(ApkRepository.sha(source)), "source untouched");
            System.out.println("PASS: actual APK signature/signer/version, ARM64 extraction, exact profile, duplicate import, rollback, tamper rejection, source preservation; static tests only");
            if (args.length == 5) {
                Path splitRoot = Files.createTempDirectory("hybrid-apk-split-tests-");
                try {
                    ApkRepository splitRepo = new ApkRepository(splitRoot.toFile(), 31, (h,s,m) -> "");
                    File base = new File(args[1]), split = new File(args[2]);
                    rejects(() -> splitRepo.importApks(Arrays.asList(base)), "required split omitted");
                    rejects(() -> splitRepo.importApks(Arrays.asList(split)), "base omitted");
                    rejects(() -> splitRepo.importApks(Arrays.asList(base, new File(args[3]))), "mixed signer");
                    rejects(() -> splitRepo.importApks(Arrays.asList(base, new File(args[4]))), "mixed versionCode");
                    ApkRepository.Version set = splitRepo.importApks(Arrays.asList(split, base));
                    check(set.info.getProperty("apk.count").equals("2"), "split count");
                    check(set.info.getProperty("engine.runtimeSupported").equals("false"), "unknown runtime disabled");
                    check(set.info.getProperty("engine.profile").startsWith("fingerprint-only:"), "unknown profile recorded");
                    splitRepo.verify(set);
                    check(splitRepo.importApks(Arrays.asList(base, split)).id().equals(set.id()), "split order independent");
                    System.out.println("PASS: signed split set, missing base/split, signer/version mismatch, order independence, unknown-engine gating; static tests only");
                } finally { ApkRepository.deleteTree(splitRoot); }
            }
        } finally { ApkRepository.deleteTree(temp); }
    }
}
