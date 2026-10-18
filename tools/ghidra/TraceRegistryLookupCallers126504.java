//@category HybridMCPE
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class TraceRegistryLookupCallers126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final Pattern CALLSITE = Pattern.compile("callsite=(0x[0-9a-f]+)");

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF, scanner report, and output path");

        Map<Long, Long> callsites = new TreeMap<>();
        boolean inTarget = false;
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            if (line.startsWith("target=")) inTarget = line.startsWith("target=0x103e5cac ");
            else if (inTarget) {
                Matcher m = CALLSITE.matcher(line);
                if (m.find()) callsites.put(Long.parseUnsignedLong(m.group(1).substring(2), 16), 0L);
            }
        }

        Map<Long, Function> functions = new TreeMap<>();
        for (long va : callsites.keySet()) {
            Address site = currentProgram.getImageBase().add(va);
            Function function = getFunctionContaining(site);
            callsites.put(va, function == null ? -1L : function.getEntryPoint().subtract(currentProgram.getImageBase()));
            if (function != null) functions.putIfAbsent(callsites.get(va), function);
        }

        try (var out = Files.newBufferedWriter(Path.of(args[1]))) {
            out.write("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase() + "\n");
            out.write("read_only=true target_lookup_elf_va=0x103e5cac scanned_callsites=" + callsites.size() +
                " containing_ghidra_functions=" + functions.size() + "\n");
            DecompInterface decompiler = new DecompInterface();
            try {
                if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
                int decompiled = 0;
                for (Function f : functions.values()) {
                    long entryVa = f.getEntryPoint().subtract(currentProgram.getImageBase());
                    long size = f.getBody().getNumAddresses();
                    out.write("\nFUNCTION elf_va=0x" + Long.toHexString(entryVa) + " name=" + f.getName() +
                        " bytes=" + size + " convention=" + f.getCallingConventionName() + " args=" + f.getParameterCount() + "\n");
                    for (var e : callsites.entrySet()) if (e.getValue() == entryVa)
                        out.write("  LOOKUP_CALL elf_va=0x" + Long.toHexString(e.getKey()) + "\n");
                    if (size > 8192 || decompiled >= 24) {
                        out.write("  DECOMPILE skipped: size_limit_or_batch_limit\n");
                        continue;
                    }
                    DecompileResults result = decompiler.decompileFunction(f, 12, monitor);
                    out.write("  DECOMPILE completed=" + result.decompileCompleted() + " error=" + result.getErrorMessage() + "\n");
                    if (result.decompileCompleted()) {
                        out.write(result.getDecompiledFunction().getC());
                        out.write("\n");
                        decompiled++;
                    }
                }
            } finally { decompiler.dispose(); }
        }
    }
}
