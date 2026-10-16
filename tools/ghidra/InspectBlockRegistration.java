//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InspectBlockRegistration extends GhidraScript {
    private static final String SHA256 = "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a";
    private final Set<Address> inspected = new HashSet<>();
    private int decompilations = 0;

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected matching engine database and output path");
        String[] terms = {"BlockTypeRegistry.cpp", "BlockTypeRegistry::getDirectAccessBlocks",
            "We need a BlockDefinitionGroup", "BlockDefinition::registerBlockDefinitionTypes",
            "BlockActor::create"};
        int[] matches = new int[terms.length];
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("sha256=" + currentProgram.getExecutableSHA256());
            out.println("image_base=" + currentProgram.getImageBase() + " language=" + currentProgram.getLanguageID());
            out.println("Existing analysis, no auto-analysis or engine execution. Targeted function creation uses unwind bounds and is rolled back.");
            DecompInterface dec = new DecompInterface();
            try {
                if (!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
                Map<Long, String> seeds = new LinkedHashMap<>();
                addSeed(seeds, out, 0x02588624L, "BlockTypeRegistry.cpp");
                addSeed(seeds, out, 0x025f6e45L, "BlockTypeRegistry::getDirectAccessBlocks");
                addSeed(seeds, out, 0x025aac68L, "We need a BlockDefinitionGroup");
                addSeed(seeds, out, 0x025f6e04L, "Tried to use the direct access blocks");
                addSeed(seeds, out, 0x0237c9efL, "mDirectAccessBlocks");
                addSeed(seeds, out, 0x0255d6b3L, "level->getBlockDefinitions()");
                addSeed(seeds, out, 0x0271a22bL, "ServerInstance::_postLevelInitialization");
                DataIterator data = currentProgram.getListing().getDefinedData(true);
                while (data.hasNext() && !monitor.isCancelled()) {
                    Data value = data.next();
                    if (!value.hasStringValue()) continue;
                    String text = String.valueOf(value.getValue());
                    for (int i = 0; i < terms.length; ++i) {
                        if (!text.contains(terms[i])) continue;
                        if (++matches[i] > 3) continue;
                        Address address = value.getAddress();
                        out.println("STRING analysis_va=" + address + " text=" + text);
                        int refs = 0;
                        ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(address);
                        while (iterator.hasNext()) {
                            Reference ref = iterator.next();
                            if (++refs > 12) continue;
                            Function f = getFunctionContaining(ref.getFromAddress());
                            out.println("REF from=" + ref.getFromAddress() + " type=" + ref.getReferenceType() +
                                " function=" + (f == null ? "unknown" : f.getEntryPoint() + " " + f.getName()));
                            Instruction instruction = getInstructionAt(ref.getFromAddress());
                            if (instruction != null) {
                                for (int n = 0; n < 5 && instruction.getPrevious() != null; ++n)
                                    instruction = instruction.getPrevious();
                                for (int n = 0; n < 14 && instruction != null; ++n, instruction = instruction.getNext())
                                    out.println("ASM " + instruction.getAddress() + " " + instruction);
                            }
                            inspectFunction(f, dec, out);
                        }
                        out.println("reference_count=" + refs);
                    }
                }
                for (int i = 0; i < terms.length; ++i) out.println("defined_string_matches " + terms[i] + "=" + matches[i]);
                scanAdjacentAddressPairs(seeds, dec, out);
                inspectUnwindBoundedGetter(dec, out);
                out.println("Limit: missing references or named functions in this database do not prove runtime absence.");
            } finally { dec.dispose(); }
        }
    }

    private void inspectUnwindBoundedGetter(DecompInterface dec, PrintWriter out) throws Exception {
        Address base = currentProgram.getImageBase();
        Address fde = base.add(0x056912a0L);
        var memory = currentProgram.getMemory();
        long displacement = Integer.toUnsignedLong(memory.getInt(fde.add(4)));
        Address cie = fde.add(4).subtract(displacement);
        if (memory.getInt(cie) != 16 || memory.getInt(cie.add(4)) != 0 ||
            memory.getByte(cie.add(8)) != 1 || memory.getByte(cie.add(9)) != 'z' ||
            memory.getByte(cie.add(10)) != 'R' || memory.getByte(cie.add(16)) != 0x1b)
            throw new IllegalStateException("Unrecognized CIE; no function boundary created");
        Address start = fde.add(8).add(memory.getInt(fde.add(8)));
        int bytes = memory.getInt(fde.add(12));
        if (!start.equals(base.add(0x103e7800L)) || bytes != 116)
            throw new IllegalStateException("Unwind bounds differ; no function boundary created");
        out.println("FDE analysis_va=" + fde + " cie=" + cie + " start=" + start + " bytes=" + bytes + " encoding=pcrel_sdata4");
        int transaction = currentProgram.startTransaction("Temporary unwind-bounded getter analysis");
        try {
            AddressSet body = new AddressSet(start, start.add(bytes - 1));
            DisassembleCommand command = new DisassembleCommand(start, body, true);
            if (!command.applyTo(currentProgram, monitor)) throw new IllegalStateException("Targeted disassembly failed");
            Function function = getFunctionAt(start);
            if (function == null) function = currentProgram.getFunctionManager().createFunction(
                "research_registry_getter_candidate", start, body, SourceType.ANALYSIS);
            Instruction instruction = getInstructionAt(start);
            while (instruction != null && body.contains(instruction.getAddress())) {
                out.println("GETTER_ASM " + instruction.getAddress() + " " + instruction);
                instruction = instruction.getNext();
            }
            dec.flushCache();
            inspectFunction(function, dec, out);
        } finally {
            currentProgram.endTransaction(transaction, false);
            dec.flushCache();
            out.println("Temporary getter function/instructions rolled back; inferred signature is not a callable contract.");
        }
    }

    private void addSeed(Map<Long, String> seeds, PrintWriter out, long elfVa, String expected) throws Exception {
        Address raw = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(elfVa);
        for (Address candidate : new Address[]{raw, currentProgram.getImageBase().add(elfVa)}) {
            StringBuilder text = new StringBuilder();
            try {
                for (int i = 0; i < 256; ++i) {
                    int value = currentProgram.getMemory().getByte(candidate.add(i)) & 0xff;
                    if (value == 0) break;
                    text.append((char) value);
                }
            } catch (Exception ignored) { continue; }
            if (!text.toString().contains(expected)) continue;
            seeds.put(candidate.getOffset(), expected);
            out.println("BYTE_VERIFIED_STRING elf_va=0x" + Long.toHexString(elfVa) +
                " analysis_va=" + candidate + " text=" + text);
            return;
        }
        throw new IllegalStateException("Seed bytes do not match: " + expected);
    }

    private int word(byte[] bytes, int index) {
        return (bytes[index] & 255) | ((bytes[index + 1] & 255) << 8) |
            ((bytes[index + 2] & 255) << 16) | ((bytes[index + 3] & 255) << 24);
    }

    private void scanAdjacentAddressPairs(Map<Long, String> seeds, DecompInterface dec, PrintWriter out) throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) throw new IllegalStateException("Missing .text block");
        Map<Long, Integer> counts = new LinkedHashMap<>();
        out.println("SCAN text_start=" + text.getStart() + " bytes=" + text.getSize());
        for (long offset = 0; offset < text.getSize() && !monitor.isCancelled(); offset += 1048576) {
            int length = (int) Math.min(1048580, text.getSize() - offset);
            byte[] bytes = new byte[length];
            currentProgram.getMemory().getBytes(text.getStart().add(offset), bytes);
            for (int i = 0; i + 8 <= length && i < 1048576; i += 4) {
                int adrp = word(bytes, i), add = word(bytes, i + 4);
                if ((adrp & 0x9f000000) != 0x90000000 || (add & 0xff000000) != 0x91000000 ||
                    (adrp & 31) == 31 || (adrp & 31) != ((add >>> 5) & 31)) continue;
                long imm = (((adrp >>> 5) & 0x7ffffL) << 2) | ((adrp >>> 29) & 3);
                if ((imm & 0x100000L) != 0) imm -= 0x200000L;
                Address pc = text.getStart().add(offset + i);
                long page = (pc.getOffset() & ~4095L) + (imm << 12);
                long target = page + (((add >>> 10) & 4095L) << (((add >>> 22) & 1) * 12));
                if (!seeds.containsKey(target)) continue;
                int count = counts.getOrDefault(target, 0) + 1;
                counts.put(target, count);
                if (count > 8) continue;
                out.println("PAIR_REFERENCE analysis_va=" + pc + " target=0x" + Long.toHexString(target) +
                    " term=" + seeds.get(target) + " words=" + Integer.toHexString(adrp) + "," + Integer.toHexString(add));
                Instruction instruction = getInstructionAt(pc);
                for (int n = 0; n < 12 && instruction != null; ++n, instruction = instruction.getNext())
                    out.println("PAIR_ASM " + instruction.getAddress() + " " + instruction);
                inspectFunction(getFunctionContaining(pc), dec, out);
            }
        }
        for (var seed : seeds.entrySet()) out.println("adjacent_pair_count " + seed.getValue() + "=" + counts.getOrDefault(seed.getKey(), 0));
        out.println("Pair scan covers adjacent ADRP/ADD only; it does not establish function identity or invocation safety.");
    }

    private void inspectFunction(Function function, DecompInterface dec, PrintWriter out) {
        if (function == null || !inspected.add(function.getEntryPoint())) return;
        long bytes = function.getBody().getNumAddresses();
        out.println("FUNCTION entry=" + function.getEntryPoint() + " bytes=" + bytes +
            " convention=" + function.getCallingConventionName() + " parameters=" + function.getParameterCount());
        if (bytes > 32768 || decompilations >= 4) {
            out.println("decompile=skipped; size/count budget; function boundaries may be inaccurate");
            return;
        }
        ++decompilations;
        var result = dec.decompileFunction(function, 20, monitor);
        out.println("PSEUDOCODE " + function.getEntryPoint());
        out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
    }
}
