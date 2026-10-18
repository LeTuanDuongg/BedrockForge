//@category HybridMCPE
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

/** Bounded xref inspection for state/palette diagnostics in the exact 1.26.50.4 ELF. */
public class TraceBlockStatePipeline126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final long[][] STRINGS = {
        {0x245e711L}, {0x2464b24L}, {0x249d88dL}, {0x241442bL},
        {0x25aac2aL}, {0x24c0a91L}, {0x242f28fL}, {0x24c8929L}
    };
    private static final String[] TERMS = {
        "Failed to create Block State Description", "bad palette", "BlockPalette",
        "block runtime ID", "BlockDefinitionGroup", "Target Runtime ID",
        "Failed to set palette for grass resource", "replaceBlockStates"
    };
    private final Set<String> seen = new HashSet<>();
    private int decompiled;

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF and output path");
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase());
            out.println("Bounded string xrefs only; addresses are Ghidra addresses, subtract image base for ELF VA.");
            DecompInterface dec = new DecompInterface();
            try {
                if (!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
                for (int i = 0; i < TERMS.length && !monitor.isCancelled(); ++i) {
                    Address address = currentProgram.getImageBase().add(STRINGS[i][0]);
                    inspectString(address, TERMS[i], dec, out);
                }
                for (long elfVa : new long[] {
                        0x1048f1b0L, 0x1048f3d4L, 0x1051ebe4L, 0x10483038L,
                        0x10487d40L, 0xec39110L, 0xb1e8678L, 0x103e71acL})
                    inspectAnchor(elfVa, dec, out);
                inspectTargetedCalls(0x10487d40L, out);
                out.println("decompiled=" + decompiled + " cap=4");
            } finally { dec.dispose(); }
        }
    }

    private void inspectTargetedCalls(long elfVa, PrintWriter out) {
        Address entry = currentProgram.getImageBase().add(elfVa);
        Function function = getFunctionAt(entry);
        if (function == null) {
            out.println("TARGETED_CALLS missing_function elf_va=0x" + Long.toHexString(elfVa));
            return;
        }
        Set<Long> targets = Set.of(0x103e2d54L, 0x103e5648L, 0x103e5cacL,
                0x1048f1b0L, 0x1048f3d4L, 0x93c3e50L, 0x100fb840L);
        int calls = 0, matches = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            calls++;
            for (Reference ref : currentProgram.getReferenceManager().getReferencesFrom(ins.getAddress())) {
                if (!ref.getReferenceType().isCall()) continue;
                long targetVa = ref.getToAddress().getOffset() - currentProgram.getImageBase().getOffset();
                if (targets.contains(targetVa)) {
                    out.println("TARGETED_CALL from_elf_va=0x" +
                        Long.toHexString(ins.getAddress().getOffset() - currentProgram.getImageBase().getOffset()) +
                        " to_elf_va=0x" + Long.toHexString(targetVa) + " instruction=" + ins);
                    matches++;
                }
            }
        }
        out.println("TARGETED_CALLS_SUMMARY function_elf_va=0x" + Long.toHexString(elfVa) +
            " call_instructions=" + calls + " selected_matches=" + matches);
    }

    private void inspectAnchor(long elfVa, DecompInterface dec, PrintWriter out) {
        Address entry = currentProgram.getImageBase().add(elfVa);
        Function function = getFunctionAt(entry);
        out.println("\nANCHOR elf_va=0x" + Long.toHexString(elfVa) + " ghidra=" + entry +
            " function=" + (function == null ? "missing" : function.getName()) +
            " bytes=" + (function == null ? -1 : function.getBody().getNumAddresses()));
        if (function == null || function.getBody().getNumAddresses() > 16384 || decompiled >= 4) return;
        ++decompiled;
        var result = dec.decompileFunction(function, 15, monitor);
        out.println("ANCHOR_PSEUDOCODE complete=" + result.decompileCompleted());
        if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
        else out.println("ANCHOR_DECOMPILE_ERROR " + result.getErrorMessage());
    }

    private void inspectString(Address address, String text, DecompInterface dec, PrintWriter out) {
        out.println("\nSTRING address=" + address + " elf_va=0x" +
            Long.toHexString(address.getOffset() - currentProgram.getImageBase().getOffset()) + " text=" + text);
        Address page = address.getNewAddress(address.getOffset() & ~0xfffL);
        List<Reference> references = new ArrayList<>();
        Set<String> keys = new HashSet<>();
        for (Address target : new Address[] {address, page}) {
            ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(target);
            while (iterator.hasNext()) {
                Reference ref = iterator.next();
                if (keys.add(ref.getFromAddress().toString() + ":" + ref.getToAddress())) references.add(ref);
            }
        }
        Iterator<Reference> refs = references.iterator();
        int count = 0;
        while (refs.hasNext() && count < 8) {
            Reference ref = refs.next();
            Function function = getFunctionContaining(ref.getFromAddress());
            out.println("REF from=" + ref.getFromAddress() + " type=" + ref.getReferenceType() +
                " function=" + (function == null ? "unknown" : function.getEntryPoint() + " " + function.getName()) +
                " body_bytes=" + (function == null ? -1 : function.getBody().getNumAddresses()));
            Instruction ins = getInstructionAt(ref.getFromAddress());
            if (ins != null) {
                for (int i = 0; i < 4 && ins.getPrevious() != null; ++i) ins = ins.getPrevious();
                for (int i = 0; i < 12 && ins != null; ++i, ins = ins.getNext())
                    out.println("ASM " + ins.getAddress() + " " + ins);
            }
            if (function != null) inspectCalls(function, out);
            if (function != null && decompiled < 4 && function.getBody().getNumAddresses() <= 8192 &&
                    seen.add(function.getEntryPoint().toString())) {
                ++decompiled;
                var result = dec.decompileFunction(function, 15, monitor);
                out.println("PSEUDOCODE entry=" + function.getEntryPoint() + " complete=" +
                    result.decompileCompleted());
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("DECOMPILE_ERROR " + result.getErrorMessage());
            }
            ++count;
        }
        out.println("xref_sample_count=" + count);
    }

    private void inspectCalls(Function function, PrintWriter out) {
        if (!seen.add("calls:" + function.getEntryPoint())) return;
        InstructionIterator it = currentProgram.getListing().getInstructions(function.getBody(), true);
        int count = 0;
        while (it.hasNext() && count < 80) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            for (Reference ref : currentProgram.getReferenceManager().getReferencesFrom(ins.getAddress())) {
                if (ref.getReferenceType().isCall()) out.println("CALL " + ins.getAddress() + " -> " + ref.getToAddress());
            }
            ++count;
        }
        out.println("call_instructions_sampled=" + count);
    }
}
