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
                for (MemoryBlock block : currentProgram.getMemory().getBlocks())
                    if (block.getName().contains("eh_frame")) out.println("UNWIND_BLOCK name=" + block.getName() + " start=" + block.getStart() + " bytes=" + block.getSize());
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
        Address getter = currentProgram.getImageBase().add(0x103e7800L);
        int getterCalls = 0;
        List<Address> getterCallSites = new ArrayList<>();
        out.println("SCAN text_start=" + text.getStart() + " bytes=" + text.getSize());
        for (long offset = 0; offset < text.getSize() && !monitor.isCancelled(); offset += 1048576) {
            int length = (int) Math.min(1048580, text.getSize() - offset);
            byte[] bytes = new byte[length];
            currentProgram.getMemory().getBytes(text.getStart().add(offset), bytes);
            for (int i = 0; i + 8 <= length && i < 1048576; i += 4) {
                int adrp = word(bytes, i), add = word(bytes, i + 4);
                if ((adrp & 0xfc000000) == 0x94000000) {
                    int imm26 = adrp & 0x03ffffff;
                    if ((imm26 & 0x02000000) != 0) imm26 |= 0xfc000000;
                    Address pc = text.getStart().add(offset + i);
                    Address target = pc.add(((long) imm26) << 2);
                    if (target.equals(getter)) {
                        ++getterCalls;
                        getterCallSites.add(pc);
                    }
                }
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
        out.println("direct_bl_to_getter_count=" + getterCalls + " target_analysis_va=" + getter);
        inspectGetterCallers(getterCallSites, dec, out);
        for (var seed : seeds.entrySet()) out.println("adjacent_pair_count " + seed.getValue() + "=" + counts.getOrDefault(seed.getKey(), 0));
        out.println("Raw .text BL scan finds direct calls only; indirect calls and unrecognized control flow remain unresolved.");
        out.println("Pair scan covers adjacent ADRP/ADD only; it does not establish function identity or invocation safety.");
    }

    private String functionLabel(Function function) {
        return function == null ? "unknown" : function.getEntryPoint() + " " + function.getName();
    }

    private void inspectGetterCallers(List<Address> callSites, DecompInterface dec, PrintWriter out) throws Exception {
        MemoryBlock header = currentProgram.getMemory().getBlock(".eh_frame_hdr");
        if (header == null) {
            out.println("CALLER_UNWIND=unavailable; .eh_frame_hdr missing");
            return;
        }
        long count = Integer.toUnsignedLong(currentProgram.getMemory().getInt(header.getStart().add(8)));
        if (count == 0 || count > 1000000) throw new IllegalStateException("Unexpected unwind table count " + count);
        out.println("CALLER_UNWIND_TABLE header=" + header.getStart() + " entries=" + count + " encoding=datarel_sdata4,pcrel_sdata4");
        Set<Address> sampledFunctions = new HashSet<>();
        for (Address callSite : callSites) {
            long low = 0, high = count;
            while (low < high) {
                long mid = (low + high) >>> 1;
                Address entry = header.getStart().add(12 + mid * 8);
                Address functionStart = header.getStart().add(currentProgram.getMemory().getInt(entry));
                if (functionStart.compareTo(callSite) <= 0) low = mid + 1;
                else high = mid;
            }
            if (low == 0) continue;
            Address entry = header.getStart().add(12 + (low - 1) * 8);
            Address functionStart = header.getStart().add(currentProgram.getMemory().getInt(entry));
            Address fde = header.getStart().add(currentProgram.getMemory().getInt(entry.add(4)));
            long cieDistance = Integer.toUnsignedLong(currentProgram.getMemory().getInt(fde.add(4)));
            Address cie = fde.add(4).subtract(cieDistance);
            long cieLength = Integer.toUnsignedLong(currentProgram.getMemory().getInt(cie));
            int encoding = currentProgram.getMemory().getByte(cie.add(16)) & 0xff;
            Address fdeStart = fde.add(8).add(currentProgram.getMemory().getInt(fde.add(8)));
            long bytes = Integer.toUnsignedLong(currentProgram.getMemory().getInt(fde.add(12)));
            if (cieLength != 16 || currentProgram.getMemory().getByte(cie.add(8)) != 1 ||
                currentProgram.getMemory().getByte(cie.add(9)) != 'z' || currentProgram.getMemory().getByte(cie.add(10)) != 'R' ||
                encoding != 0x1b || !functionStart.equals(fdeStart) || bytes == 0 || bytes > 1048576 ||
                callSite.compareTo(functionStart) < 0 || callSite.compareTo(functionStart.add(bytes)) >= 0) {
                out.println("CALLER_FDE_SKIPPED_UNSUPPORTED callsite=" + callSite + " table_start=" + functionStart + " fde_start=" + fdeStart +
                    " bytes=" + bytes + " cie_length=" + cieLength + " encoding=0x" + Integer.toHexString(encoding));
                continue;
            }
            if (!sampledFunctions.add(functionStart)) continue;
            out.println("CALLER_FDE callsite=" + callSite + " elf_callsite=0x" + Long.toHexString(callSite.getOffset() - currentProgram.getImageBase().getOffset()) +
                " start=" + functionStart + " elf_start=0x" + Long.toHexString(functionStart.getOffset() - currentProgram.getImageBase().getOffset()) +
                " bytes=" + bytes + " fde=" + fde);
            if (sampledFunctions.size() > 4) continue;
            int tx = currentProgram.startTransaction("Temporary unwind-bounded getter caller analysis");
            try {
                AddressSet body = new AddressSet(functionStart, functionStart.add(bytes - 1));
                DisassembleCommand command = new DisassembleCommand(functionStart, body, true);
                if (!command.applyTo(currentProgram, monitor)) {
                    out.println("CALLER_DISASSEMBLY=failed start=" + functionStart);
                    continue;
                }
                Function function = getFunctionAt(functionStart);
                if (function == null) function = currentProgram.getFunctionManager().createFunction(
                    "research_getter_caller", functionStart, body, SourceType.ANALYSIS);
                Instruction instruction = getInstructionAt(callSite.add(-16));
                for (int n = 0; n < 12 && instruction != null && body.contains(instruction.getAddress()); ++n, instruction = instruction.getNext())
                    out.println("CALLER_ASM " + instruction.getAddress() + " " + instruction);
                dec.flushCache();
                inspectFunction(function, dec, out);
            } finally {
                currentProgram.endTransaction(tx, false);
                dec.flushCache();
            }
        }
        out.println("Caller scan covers all direct call sites; detailed decompilation is limited to 4 unwind-validated functions and is not a complete call graph.");
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
