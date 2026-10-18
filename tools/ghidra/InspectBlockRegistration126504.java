//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InspectBlockRegistration126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private final Set<String> decompiled = new HashSet<>();
    private int decompilations;

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected Minecraft 1.26.50.4 ELF and output path");
        String[] terms = {"BlockTypeRegistry.cpp", "mDirectAccessBlocks", "getDirectAccessBlocks()",
            "direct access blocks without initializing", "already registered with a different ID",
            "already registered with material", "not already registered and no blockID was specified",
            "BlockDefinitionGroup", "registerBlockDefinitionTypes", "level->getBlockDefinitions()",
            "BlockTypeRegistry checksums did not match"};
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase() +
                " language=" + currentProgram.getLanguageID());
            out.println("Source: exact 1.26.50.4 ELF. Ghidra analysis only; no engine execution or invocation.");
            DecompInterface dec = new DecompInterface();
            try {
                if (!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
                int[] found = new int[terms.length];
                DataIterator data = currentProgram.getListing().getDefinedData(true);
                while (data.hasNext() && !monitor.isCancelled()) {
                    Data value = data.next();
                    if (!value.hasStringValue()) continue;
                    String text = String.valueOf(value.getValue());
                    for (int i = 0; i < terms.length; ++i) {
                        if (!text.contains(terms[i]) || found[i]++ >= 4) continue;
                        Address address = value.getAddress();
                        out.println("STRING address=" + address + " text=" + text);
                        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
                        int count = 0;
                        while (refs.hasNext()) {
                            Reference ref = refs.next();
                            if (++count > 24) continue;
                            Function function = getFunctionContaining(ref.getFromAddress());
                            out.println("REF from=" + ref.getFromAddress() + " type=" + ref.getReferenceType() +
                                " function=" + (function == null ? "unknown" : function.getEntryPoint() + " " + function.getName()));
                            Instruction instruction = getInstructionAt(ref.getFromAddress());
                            if (instruction != null) {
                                for (int n = 0; n < 4 && instruction.getPrevious() != null; ++n) instruction = instruction.getPrevious();
                                for (int n = 0; n < 12 && instruction != null; ++n, instruction = instruction.getNext())
                                    out.println("ASM " + instruction.getAddress() + " " + instruction);
                            }
                            inspect(function, dec, out);
                        }
                        out.println("reference_count=" + count);
                    }
                }
                for (int i = 0; i < terms.length; ++i) out.println("defined_string_matches " + terms[i] + "=" + found[i]);
                int named = 0;
                for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                    String name = f.getName();
                    if (!(name.contains("BlockTypeRegistry") || name.contains("BlockDefinitionGroup") ||
                        name.contains("registerBlockDefinitionTypes"))) continue;
                    out.println("NAMED_FUNCTION entry=" + f.getEntryPoint() + " name=" + name +
                        " bytes=" + f.getBody().getNumAddresses() + " convention=" + f.getCallingConventionName() +
                        " parameters=" + f.getParameterCount());
                    inspect(f, dec, out);
                    if (++named >= 40) break;
                }
                out.println("Named candidate functions reported=" + named + ". Names/xrefs alone do not establish a callable ABI.");
            } finally { dec.dispose(); }
        }
    }

    private void inspect(Function function, DecompInterface dec, PrintWriter out) {
        if (function == null || !decompiled.add(function.getEntryPoint().toString())) return;
        long bytes = function.getBody().getNumAddresses();
        out.println("FUNCTION entry=" + function.getEntryPoint() + " name=" + function.getName() +
            " bytes=" + bytes + " convention=" + function.getCallingConventionName() +
            " parameters=" + function.getParameterCount());
        if (bytes > 32768 || decompilations >= 12) { out.println("decompile=skipped; bounded budget"); return; }
        ++decompilations;
        var result = dec.decompileFunction(function, 30, monitor);
        out.println("PSEUDOCODE " + function.getEntryPoint());
        out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
    }
}
