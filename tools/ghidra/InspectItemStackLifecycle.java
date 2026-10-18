//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class InspectItemStackLifecycle extends GhidraScript {
    private static final String TARGET_SHA256 = "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a";

    @Override
    public void run() throws Exception {
        if (!TARGET_SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong engine database");

        println("target_sha256=" + currentProgram.getExecutableSHA256());
        println("program=" + currentProgram.getName() + " image_base=" + currentProgram.getImageBase());
        println("Static Ghidra evidence only; no function is created or invoked.");

        int nameMatches = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            if (!function.getName().toLowerCase().contains("itemstack")) continue;
            nameMatches++;
            println("named_function address=" + function.getEntryPoint() + " name=" + function.getName() +
                " convention=" + function.getCallingConventionName() + " params=" + function.getParameterCount() +
                " return=" + function.getReturnType().getDisplayName());
        }
        println("named_function_count=" + nameMatches);

        int symbolMatches = 0;
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext() && !monitor.isCancelled()) {
            Symbol symbol = symbols.next();
            if (!symbol.getName().toLowerCase().contains("itemstack")) continue;
            symbolMatches++;
            println("named_symbol address=" + symbol.getAddress() + " type=" + symbol.getSymbolType() +
                " name=" + symbol.getName());
        }
        println("named_symbol_count=" + symbolMatches);

        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram))
                throw new IllegalStateException(decompiler.getLastMessage());
            inspectCandidate("ItemStack_vtable_slot0_destructor_candidate", 0x085ae038L, decompiler);
            inspectCandidate("ItemStack_alternate_cleanup_candidate", 0x0ffa9474L, decompiler);
            inspectVtable(0x1318be60L);
        } finally {
            decompiler.dispose();
        }
    }

    private void inspectCandidate(String label, long elfVa, DecompInterface decompiler) {
        Address rawAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(elfVa);
        inspectAddress(label + "_elf_va", rawAddress, decompiler);
        Address adjustedAddress = currentProgram.getImageBase().add(elfVa);
        if (!adjustedAddress.equals(rawAddress))
            inspectAddress(label + "_image_base_adjusted", adjustedAddress, decompiler);
    }

    private void inspectAddress(String label, Address address, DecompInterface decompiler) {
        Function function = getFunctionAt(address);
        if (function == null) function = getFunctionContaining(address);
        println("\nCANDIDATE " + label + " address=" + address);
        int referenceCount = 0;
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            referenceCount++;
            if (referenceCount > 12) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("reference type=" + ref.getReferenceType() + " from=" + ref.getFromAddress() + " caller=" +
                (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
            if (ref.getReferenceType().isCall()) {
                Instruction instruction = currentProgram.getListing().getInstructionAt(ref.getFromAddress());
                Instruction first = instruction;
                for (int i = 0; i < 4 && first != null && first.getPrevious() != null; ++i)
                    first = first.getPrevious();
                for (int i = 0; i < 9 && first != null; ++i, first = first.getNext())
                    println("  caller_asm " + first.getAddress() + " " + first);
            }
        }
        println("reference_count=" + referenceCount);
        if (referenceCount > 0 || label.startsWith("ItemStack_")) {
            Instruction instruction = currentProgram.getListing().getInstructionAt(address);
            for (int i = 0; i < 48 && instruction != null; ++i, instruction = instruction.getNext())
                println("candidate_asm " + instruction.getAddress() + " " + instruction);
        }
        if (function == null) {
            println("function=unresolved; existing analysis has no function at candidate");
            Instruction instruction = currentProgram.getListing().getInstructionAt(address);
            println("instruction=" + (instruction == null ? "none" : instruction));
            return;
        }

        long size = function.getBody().getMaxAddress().subtract(function.getBody().getMinAddress()) + 1;
        boolean exact = function.getEntryPoint().equals(address);
        println("function=" + function.getName() + " entry=" + function.getEntryPoint() +
            " end=" + function.getBody().getMaxAddress() + " exact_entry=" + exact + " body_bytes=" + size);
        println("database_signature return=" + function.getReturnType().getDisplayName() +
            " convention=" + function.getCallingConventionName() + " parameters=" + function.getParameterCount());
        for (Parameter parameter : function.getParameters())
            println("parameter ordinal=" + parameter.getOrdinal() + " type=" +
                parameter.getDataType().getDisplayName() + " name=" + parameter.getName());

        int callerCount = 0;
        refs = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        while (refs.hasNext()) {
            Reference ref = refs.next();
            if (!ref.getReferenceType().isCall()) continue;
            callerCount++;
            if (callerCount > 12) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("direct_call_ref site=" + ref.getFromAddress() + " caller=" +
                (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
            Instruction instruction = currentProgram.getListing().getInstructionAt(ref.getFromAddress());
            Instruction first = instruction;
            for (int i = 0; i < 4 && first != null && first.getPrevious() != null; ++i)
                first = first.getPrevious();
            for (int i = 0; i < 9 && first != null; ++i, first = first.getNext())
                println("  caller_asm " + first.getAddress() + " " + first);
        }
        println("direct_call_ref_count=" + callerCount + " (indirect references may be absent)");

        if (exact && size <= 0x10000) {
            var result = decompiler.decompileFunction(function, 30, monitor);
            println("decompile=" + (result.decompileCompleted() ? "complete" : result.getErrorMessage()));
            if (result.decompileCompleted()) println(result.getDecompiledFunction().getC());
        } else {
            println("decompile=skipped; function boundary is not exact or exceeds 64 KiB");
        }
    }

    private void inspectVtable(long addressValue) throws Exception {
        Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addressValue);
        println("\nVTABLE candidate address=" + address);
        long typeInfo = currentProgram.getMemory().getLong(address.subtract(8));
        println("typeinfo_pointer=0x" + Long.toHexString(typeInfo));
        Address typeInfoAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(typeInfo);
        long typeName = currentProgram.getMemory().getLong(typeInfoAddress.add(8));
        println("type_name_pointer=0x" + Long.toHexString(typeName));
        Address typeNameAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(typeName);
        StringBuilder name = new StringBuilder();
        for (int i = 0; i < 96; ++i) {
            int value = currentProgram.getMemory().getByte(typeNameAddress.add(i)) & 0xff;
            if (value == 0) break;
            name.append((char)value);
        }
        println("type_name=" + name);
        for (int i = 0; i < 6; ++i)
            println("vtable_slot[" + i + "]=0x" + Long.toHexString(currentProgram.getMemory().getLong(address.add(i * 8L))));

        int count = 0;
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            count++;
            if (count > 16) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("vtable_ref type=" + ref.getReferenceType() + " from=" + ref.getFromAddress() + " caller=" +
                (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
            Instruction instruction = currentProgram.getListing().getInstructionAt(ref.getFromAddress());
            Instruction first = instruction;
            for (int i = 0; i < 3 && first != null && first.getPrevious() != null; ++i)
                first = first.getPrevious();
            for (int i = 0; i < 7 && first != null; ++i, first = first.getNext())
                println("  vtable_asm " + first.getAddress() + " " + first);
        }
        println("vtable_reference_count=" + count);
    }
}
