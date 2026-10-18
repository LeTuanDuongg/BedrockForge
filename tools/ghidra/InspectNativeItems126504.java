//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.app.util.PseudoDisassembler;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.reloc.Relocation;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

public class InspectNativeItems126504 extends GhidraScript {
    private static final String TARGET_SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";

    @Override
    public void run() throws Exception {
        if (!TARGET_SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong Minecraft 1.26.50.4 ELF");

        println("sha256=" + currentProgram.getExecutableSHA256());
        println("program=" + currentProgram.getName() + " language=" + currentProgram.getLanguageID() +
            " image_base=" + currentProgram.getImageBase());
        println("read_only=true; no analysis, labels, functions, or memory changes");
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            println("memory_block=" + block.getName() + " start=" + block.getStart() + " end=" +
                block.getEnd() + " initialized=" + block.isInitialized() + " execute=" + block.isExecute());
        }

        long functionCount = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            functions.next();
            functionCount++;
        }
        println("existing_functions=" + functionCount);

        inspectVtable("ItemUseInventoryTransaction", 0x1309ccb0L, 0x1309dc00L,
            new long[] {0xb8455f4L, 0x100277e0L, 0x10021c70L, 0x10024858L,
                0x10024a30L, 0x10020f68L, 0x10020f24L});
        inspectVtable("ItemStack", 0x1308a6c0L, 0x1308b7e0L,
            new long[] {0x85adf18L, 0xffa8e20L, 0xff9d6a0L, 0xff9d52cL,
                0xff9d814L, 0xff9f4d0L, 0xff9f728L, 0xff9faf0L});
        findExactStrings("minecraft:apple");
        findRttiVtables("4Item", "9StickItem");
        scanGlobalUses(0x13089fd0L);
        inspectWindow("item_init_caller", 0x0fbb9e6cL, -40, 12);
        inspectRegisterSetup("item_init_outer_caller", 0x0fbb9e6cL, "x19", 8192);
        inspectWindow("stick_global_caller", 0x0ff08314L, -24, 56);
        inspectRegisterSetup("stick_global_caller", 0x0ff08314L, "x19", 16384);
        inspectDirectCallers("item_init_start", 0x100057c0L);
        inspectDirectCallers("item_init_outer", 0x0fcb9bb0L);
        inspectDirectCallers("item_init_factory_entry", 0x0fcbf7b8L);
        inspectDirectCallers("item_init_factory", 0x0fcbf814L);
        inspectWindow("item_init_outer_callsite", 0x0fbbf894L, -32, 4);
        inspectWindow("item_init_factory_begin", 0x0fbbf7b8L, -4, 64);
        inspectRegisterSetup("item_init_factory_context", 0x0fbbf894L, "x25", 8192);
        inspectRegisterSetup("item_init_factory_owner", 0x0fbbf894L, "x24", 8192);
        Function outerOwner = currentProgram.getFunctionManager().getFunctionContaining(mapped(0x0fbbf894L));
        println("item_init_factory_owner=" + outerOwner);
        if (outerOwner != null) {
            println("item_init_factory_owner_entry=" + outerOwner.getEntryPoint() + " elf=0x" +
                Long.toHexString(outerOwner.getEntryPoint().getOffset() - currentProgram.getImageBase().getOffset()));
            inspectDirectCallers("item_init_factory_owner", outerOwner.getEntryPoint().getOffset());
        }
        inspectCallTarget(0x1007cd08L, 0x0fcbbac8L, 0x0fcb16d4L);
    }

    private Address mapped(long elfVa) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(
            elfVa + currentProgram.getImageBase().getOffset());
    }

    private void inspectWindow(String label, long center, int before, int after) throws Exception {
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        Address start = mapped(center).add(before * 4L);
        println("window=" + label + " ghidra_start=" + start + " elf_start=0x" +
            Long.toHexString(start.getOffset() - currentProgram.getImageBase().getOffset()));
        for (int i = 0; i <= after - before; ++i) {
            Address at = start.add(i * 4L);
            Instruction insn = decoder.disassemble(at);
            println("  window_insn[" + (i + before) + "] @" + at + " " +
                (insn == null ? "<invalid>" : insn.toString()));
        }
    }

    private void inspectRegisterSetup(String label, long center, String register, int maxBackWords)
            throws Exception {
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        Address target = mapped(center);
        Address start = null;
        for (int i = 1; i <= maxBackWords; ++i) {
            Instruction insn = decoder.disassemble(target.subtract(i * 4L));
            if (insn != null && insn.toString().startsWith("stp x29,x30,[sp")) {
                start = target.subtract(i * 4L);
                break;
            }
        }
        if (start == null) {
            println("register_setup=" + label + " register=" + register + " prologue=not_found");
            return;
        }
        println("register_setup=" + label + " register=" + register + " candidate_prologue=" + start +
            " elf=0x" + Long.toHexString(start.getOffset() - currentProgram.getImageBase().getOffset()));
        int index = 0;
        for (Address at = start; at.compareTo(target) <= 0; at = at.add(4), ++index) {
            Instruction insn = decoder.disassemble(at);
            if (insn == null) continue;
            String text = insn.toString();
            if (text.matches("(mov|ldr|ldp|adrp|add|sub|movk) " + register + ",.*") || at.equals(target))
                println("  register_setup_insn[" + index + "] @" + at + " " + text);
        }
    }

    private void inspectDirectCallers(String label, long targetGhidra) throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) return;
        int count = 0;
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        for (Address pc = text.getStart(); pc.compareTo(text.getEnd().subtract(3)) <= 0 && !monitor.isCancelled();
                pc = pc.add(4)) {
            long word = Integer.toUnsignedLong(currentProgram.getMemory().getInt(pc));
            if ((word & 0xfc000000L) != 0x94000000L) continue;
            long imm = word & 0x03ffffffL;
            if ((imm & 0x02000000L) != 0) imm -= 0x04000000L;
            if (pc.getOffset() + (imm << 2) != targetGhidra) continue;
            count++;
            println("direct_caller label=" + label + " pc=" + pc + " elf=0x" +
                Long.toHexString(pc.getOffset() - currentProgram.getImageBase().getOffset()));
            for (int i = -5; i <= 2; ++i) {
                Instruction insn = decoder.disassemble(pc.add(i * 4L));
                println("  caller_insn[" + i + "] " + (insn == null ? "<invalid>" : insn.toString()));
            }
        }
        println("direct_caller_count label=" + label + " count=" + count);
    }

    private void inspectVtable(String label, long typeInfoVa, long addressPointVa,
            long[] expectedElfSlots) throws Exception {
        Address typeInfo = mapped(typeInfoVa);
        Address addressPoint = mapped(addressPointVa);
        println("\nclass=" + label + " typeinfo_elf=0x" + Long.toHexString(typeInfoVa) +
            " typeinfo_ghidra=" + typeInfo + " vtable_elf=0x" + Long.toHexString(addressPointVa) +
            " vtable_ghidra=" + addressPoint + " memory=" + currentProgram.getMemory().getBlock(addressPoint));
        println("typeinfo_word0=0x" + Long.toHexString(currentProgram.getMemory().getLong(typeInfo)) +
            " typeinfo_word1=0x" + Long.toHexString(currentProgram.getMemory().getLong(typeInfo.add(8))));
        for (int i = 0; i < expectedElfSlots.length; ++i) {
            long raw = currentProgram.getMemory().getLong(addressPoint.add(i * 8L));
            long expected = expectedElfSlots[i];
            println("slot[" + i + "] raw=0x" + Long.toHexString(raw) +
                " expected_elf=0x" + Long.toHexString(expected) +
                " expected_ghidra=" + mapped(expected) +
                " function=" + currentProgram.getFunctionManager().getFunctionAt(mapped(expected)));
        }
        int refs = 0;
        ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(addressPoint);
        while (iterator.hasNext()) {
            Reference ref = iterator.next();
            refs++;
            if (refs > 12) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("vtable_ref=" + ref.getFromAddress() + " caller=" +
                (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
        }
        println("vtable_reference_count=" + refs);
    }

    private void findExactStrings(String... needles) throws Exception {
        Set<Long> targetPages = new HashSet<>();
        Set<Long> targetOffsets = new HashSet<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || !block.isRead() || block.isExecute()) continue;
            String blockName = block.getName().toLowerCase();
            if (!blockName.contains("rodata")) continue;
            for (int n = 0; n < needles.length; ++n) {
                byte[] text = needles[n].getBytes(StandardCharsets.UTF_8);
                byte[] bytes = java.util.Arrays.copyOf(text, text.length + 1);
                byte[] mask = new byte[bytes.length];
                java.util.Arrays.fill(mask, (byte) 0xff);
                Address cursor = block.getStart();
                while (cursor != null && cursor.compareTo(block.getEnd()) <= 0 && !monitor.isCancelled()) {
                    Address found = currentProgram.getMemory().findBytes(cursor, block.getEnd(), bytes,
                        mask, true, monitor);
                    if (found == null) break;
                    printStringHit(needles[n], found);
                    targetOffsets.add(found.getOffset());
                    targetPages.add(found.getOffset() & ~0xfffL);
                    cursor = found.add(bytes.length);
                }
            }
        }
        scanTextForAdrp(targetPages, targetOffsets);
    }

    private void printStringHit(String needle, Address address) {
        println("raw_string=" + needle + " ghidra=" + address + " elf=0x" +
            Long.toHexString(address.getOffset() - currentProgram.getImageBase().getOffset()) + " memory=" +
            currentProgram.getMemory().getBlock(address));
        int refCount = 0;
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            refCount++;
            if (refCount > 10) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("string_xref=" + ref.getFromAddress() + " type=" + ref.getReferenceType() +
                " caller=" + (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
        }
        println("string_xref_count=" + refCount);
    }

    private void scanTextForAdrp(Set<Long> targetPages, Set<Long> targetOffsets) throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) {
            println("targeted_adrp_scan=unavailable_no_text_block");
            return;
        }
        println("targeted_adrp_scan_start=" + text.getStart() + " end=" + text.getEnd() +
            " target_pages=" + targetPages.size());
        int pageHits = 0;
        int exactHits = 0;
        Set<String> functions = new HashSet<>();
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        for (Address pc = text.getStart(); pc.compareTo(text.getEnd().subtract(3)) <= 0 && !monitor.isCancelled();
                pc = pc.add(4)) {
            int word = currentProgram.getMemory().getInt(pc);
            long insn = Integer.toUnsignedLong(word);
            if ((insn & 0x9f000000L) != 0x90000000L) continue;
            long imm = (((insn >>> 5) & 0x7ffffL) << 2) | ((insn >>> 29) & 3L);
            if ((imm & 0x100000L) != 0) imm -= 0x200000L;
            long targetPage = ((pc.getOffset() & ~0xfffL) + (imm << 12)) & ~0xfffL;
            if (!targetPages.contains(targetPage)) continue;
            pageHits++;
            int baseRegister = (int)(insn & 0x1fL);
            long exactTarget = -1;
            for (int j = 1; j <= 3; ++j) {
                long next = Integer.toUnsignedLong(currentProgram.getMemory().getInt(pc.add(j * 4L)));
                if ((next & 0x7f000000L) != 0x11000000L || (next & 0x80000000L) == 0) continue;
                int sourceRegister = (int)((next >>> 5) & 0x1fL);
                int shift = (int)((next >>> 22) & 1L);
                long immediate = ((next >>> 10) & 0xfffL) << (shift * 12);
                if (sourceRegister != baseRegister) continue;
                for (long target : targetOffsets) {
                    if ((target & ~0xfffL) == targetPage && (target - targetPage) == immediate)
                        exactTarget = target;
                }
            }
            if (exactTarget < 0) continue;
            exactHits++;
            Function fn = getFunctionContaining(pc);
            String fnKey = fn == null ? "unresolved" : fn.getEntryPoint().toString();
            functions.add(fnKey);
            if (exactHits > 32) continue;
            println("adrp_string_ref pc=" + pc + " elf=0x" +
                Long.toHexString(pc.getOffset() - currentProgram.getImageBase().getOffset()) +
                " target_page=" + mapped(targetPage - currentProgram.getImageBase().getOffset()) +
                " function=" + (fn == null ? "unresolved" : fn.getEntryPoint() + " " + fn.getName()) +
                " raw_function=" + currentProgram.getFunctionManager().getFunctionContaining(
                    currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(
                        pc.getOffset() - currentProgram.getImageBase().getOffset())));
            for (int i = -8; i < 24; ++i) {
                Address at = pc.add(i * 4L);
                Instruction instruction = decoder.disassemble(at);
                println("  insn[" + i + "] @" + at + " " +
                    (instruction == null ? "<invalid>" : instruction.toString()));
            }
        }
        println("targeted_adrp_page_hits=" + pageHits + " exact_string_address_refs=" + exactHits +
            " containing_functions=" + functions.size());
    }

    private void inspectCallTarget(long... targets) throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) return;
        Set<Long> wanted = new HashSet<>();
        for (long target : targets) {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(target);
            MemoryBlock targetBlock = currentProgram.getMemory().getBlock(address);
            println("call_target_ghidra=0x" + Long.toHexString(target) + " elf=0x" +
                Long.toHexString(target - currentProgram.getImageBase().getOffset()) + " function=" +
                currentProgram.getFunctionManager().getFunctionContaining(address) + " memory=" + targetBlock +
                " executable=" + (targetBlock != null && targetBlock.isExecute()));
            wanted.add(target);
            PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
            println("callee_disassembly_begin=0x" + Long.toHexString(target));
            int instructionLimit = targetBlock != null && targetBlock.getName().equals(".plt") ? 4 : 72;
            for (int i = 0; i < instructionLimit; ++i) {
                Instruction insn = decoder.disassemble(address.add(i * 4L));
                if (insn == null) {
                    println("  callee_insn[" + i + "]=<invalid>");
                    break;
                }
                println("  callee_insn[" + i + "] " + insn);
                if (insn.getFlowType().isTerminal()) break;
            }
            println("callee_disassembly_end=0x" + Long.toHexString(target));
            if (target == 0x12a11160L) inspectRelocation(0x133888b0L);
        }
        println("direct_bl_scan_skipped=shared helper callsites need a targeted caller question");
        if (Boolean.getBoolean("hybridmcpe.inspect.shared.callers")) {
        int calls = 0;
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        for (Address pc = text.getStart(); pc.compareTo(text.getEnd().subtract(3)) <= 0 && !monitor.isCancelled();
                pc = pc.add(4)) {
            long word = Integer.toUnsignedLong(currentProgram.getMemory().getInt(pc));
            if ((word & 0xfc000000L) != 0x94000000L) continue;
            long imm = word & 0x03ffffffL;
            if ((imm & 0x02000000L) != 0) imm -= 0x04000000L;
            long dest = pc.getOffset() + (imm << 2);
            if (!wanted.contains(dest)) continue;
            MemoryBlock destinationBlock = currentProgram.getMemory().getBlock(
                currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(dest));
            if (destinationBlock != null && destinationBlock.getName().equals(".plt")) continue;
            calls++;
            if (calls > 40) continue;
            println("direct_bl_callsite=" + pc + " elf=0x" +
                Long.toHexString(pc.getOffset() - currentProgram.getImageBase().getOffset()) +
                " target=" + Long.toHexString(dest) + " containing_function=" +
                getFunctionContaining(pc));
            for (int i = -5; i <= 2; ++i) {
                Instruction insn = decoder.disassemble(pc.add(i * 4L));
                println("  call_context[" + i + "] " + (insn == null ? "<invalid>" : insn.toString()));
            }
        }
        println("direct_bl_callsite_count=" + calls);
        }
    }

    private void inspectRelocation(long gotAddress) throws Exception {
        Address got = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(gotAddress);
        println("plt_got_slot=" + got + " value=0x" +
            Long.toHexString(currentProgram.getMemory().getLong(got)) + " block=" +
            currentProgram.getMemory().getBlock(got));
        for (Relocation relocation : currentProgram.getRelocationTable().getRelocations(got)) {
            println("got_relocation address=" + relocation.getAddress() + " type=" + relocation.getType() +
                " symbol=" + relocation.getSymbolName() + " values=" +
                java.util.Arrays.toString(relocation.getValues()));
        }
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(got))
            println("got_symbol=" + symbol.getName() + " type=" + symbol.getSymbolType() +
                " source=" + symbol.getSource());
        Address plt = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(0x12a11160L);
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(plt))
            println("plt_symbol=" + symbol.getName() + " type=" + symbol.getSymbolType() +
                " source=" + symbol.getSource());
    }

    private void scanGlobalUses(long globalAddress) throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) return;
        long mappedGlobal = globalAddress + currentProgram.getImageBase().getOffset();
        long targetPage = mappedGlobal & ~0xfffL;
        long targetOffset = mappedGlobal & 0xfffL;
        PseudoDisassembler decoder = new PseudoDisassembler(currentProgram);
        int pageHits = 0;
        int exactUses = 0;
        for (Address pc = text.getStart(); pc.compareTo(text.getEnd().subtract(3)) <= 0 && !monitor.isCancelled();
                pc = pc.add(4)) {
            long insn = Integer.toUnsignedLong(currentProgram.getMemory().getInt(pc));
            if ((insn & 0x9f000000L) != 0x90000000L) continue;
            long imm = (((insn >>> 5) & 0x7ffffL) << 2) | ((insn >>> 29) & 3L);
            if ((imm & 0x100000L) != 0) imm -= 0x200000L;
            long adrpPage = ((pc.getOffset() & ~0xfffL) + (imm << 12)) & ~0xfffL;
            if (adrpPage != targetPage) continue;
            pageHits++;
            int baseReg = (int)(insn & 0x1fL);
            boolean exact = false;
            for (int j = 1; j <= 5; ++j) {
                long next = Integer.toUnsignedLong(currentProgram.getMemory().getInt(pc.add(j * 4L)));
                if ((next & 0x7f000000L) != 0x11000000L || (next & 0x80000000L) == 0) continue;
                int rn = (int)((next >>> 5) & 0x1fL);
                int shift = (int)((next >>> 22) & 1L);
                long addend = ((next >>> 10) & 0xfffL) << (shift * 12);
                if (rn == baseReg && addend == targetOffset) exact = true;
            }
            if (!exact) continue;
            exactUses++;
            if (exactUses > 12) continue;
            println("global_object_use pc=" + pc + " elf=0x" +
                Long.toHexString(pc.getOffset() - currentProgram.getImageBase().getOffset()) +
                " global=" + mapped(globalAddress) + " following:");
            for (int j = -2; j <= 8; ++j) {
                Instruction following = decoder.disassemble(pc.add(j * 4L));
                println("  global_use_insn[" + j + "] " +
                    (following == null ? "<invalid>" : following.toString()));
            }
        }
        println("global_page_reference_count=" + pageHits + " exact_global_add_uses=" + exactUses);
    }

    private void findRttiVtables(String... names) throws Exception {
        MemoryBlock rodata = currentProgram.getMemory().getBlock(".rodata");
        MemoryBlock relro = currentProgram.getMemory().getBlock(".data.rel.ro");
        if (rodata == null || relro == null) return;
        for (String name : names) {
            byte[] text = name.getBytes(StandardCharsets.UTF_8);
            byte[] bytes = java.util.Arrays.copyOf(text, text.length + 1);
            byte[] mask = new byte[bytes.length];
            java.util.Arrays.fill(mask, (byte)0xff);
            Address cursor = rodata.getStart();
            int matches = 0;
            while (cursor.compareTo(rodata.getEnd()) <= 0 && !monitor.isCancelled()) {
                Address found = currentProgram.getMemory().findBytes(cursor, rodata.getEnd(), bytes, mask, true, monitor);
                if (found == null) break;
                matches++;
                println("rtti_name=" + name + " ghidra=" + found + " elf=0x" +
                    Long.toHexString(found.getOffset() - currentProgram.getImageBase().getOffset()));
                long namePointer = found.getOffset();
                int typeInfoCount = 0;
                for (Address slot = relro.getStart(); slot.compareTo(relro.getEnd().subtract(7)) <= 0;
                        slot = slot.add(8)) {
                    if (currentProgram.getMemory().getLong(slot) != namePointer) continue;
                    Address typeInfo = slot.subtract(8);
                    typeInfoCount++;
                    println("  typeinfo=" + typeInfo + " name_slot=" + slot + " word0=0x" +
                        Long.toHexString(currentProgram.getMemory().getLong(typeInfo)));
                    int refs = 0;
                    int vtables = 0;
                    for (Address refSlot = relro.getStart(); refSlot.compareTo(relro.getEnd().subtract(7)) <= 0;
                            refSlot = refSlot.add(8)) {
                        if (currentProgram.getMemory().getLong(refSlot) != typeInfo.getOffset()) continue;
                        refs++;
                        Address addressPoint = refSlot.add(8);
                        long first = currentProgram.getMemory().getLong(addressPoint);
                        Address firstAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(first);
                        MemoryBlock firstBlock = currentProgram.getMemory().getBlock(firstAddress);
                        if (firstBlock == null || !firstBlock.isExecute()) continue;
                        vtables++;
                        if (vtables > 12) continue;
                        println("    vtable_candidate=" + addressPoint + " offset_to_top=0x" +
                            Long.toHexString(currentProgram.getMemory().getLong(refSlot.subtract(8))) + " first_slots:");
                        for (int i = 0; i < 8; ++i) {
                            long value = currentProgram.getMemory().getLong(addressPoint.add(i * 8L));
                            Address function = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                            MemoryBlock functionBlock = currentProgram.getMemory().getBlock(function);
                            println("      slot[" + i + "]=0x" + Long.toHexString(value) +
                                " executable=" + (functionBlock != null && functionBlock.isExecute()));
                        }
                    }
                    println("  typeinfo_pointer_reference_count=" + refs + " executable_vtable_candidates=" + vtables);
                }
                println("  typeinfo_candidate_count=" + typeInfoCount);
                cursor = found.add(bytes.length);
            }
            println("rtti_name_match_count[" + name + "]=" + matches);
        }
    }

    private void searchDefinedStrings(String... needles) throws Exception {
        for (String needle : needles) {
            int matches = 0;
            DataIterator data = currentProgram.getListing().getDefinedData(true);
            while (data.hasNext() && !monitor.isCancelled()) {
                Data item = data.next();
                if (!item.hasStringValue()) continue;
                Object value = item.getValue();
                if (value == null || !value.toString().contains(needle)) continue;
                matches++;
                if (matches > 8) continue;
                int refCount = 0;
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    refCount++;
                    if (refCount > 6) continue;
                    Function caller = getFunctionContaining(ref.getFromAddress());
                    println("string=" + needle + " at=" + item.getAddress() + " xref=" + ref.getFromAddress() +
                        " caller=" + (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
                }
                if (refCount == 0) println("string=" + needle + " at=" + item.getAddress() + " xrefs=0");
            }
            println("string_match_count[" + needle + "]=" + matches);
        }
    }
}
