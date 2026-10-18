//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class TraceRuntimeRegistryWriter126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ARM64 ELF and one ELF VA");

        Address elfVa = toAddr(args[0]);
        Address pc = currentProgram.getImageBase().add(elfVa.getOffset());
        Function function = getFunctionContaining(pc);
        println("target_sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase() +
            " writer_pc_elf_va=" + elfVa + " writer_pc_ghidra=" + pc);
        if (function == null) {
            println("function=unresolved; existing database has no containing function at this address");
        } else {
            println("function_entry=" + function.getEntryPoint() + " name=" + function.getName() +
                " body_bytes=" + function.getBody().getNumAddresses() +
                " convention=" + function.getCallingConventionName() +
                " parameters=" + function.getParameterCount());
            ReferenceIterator callers = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
            int count = 0;
            while (callers.hasNext() && count < 40) {
                Reference reference = callers.next();
                Address from = reference.getFromAddress();
                Function caller = getFunctionContaining(from);
                println("incoming_ref=" + from + " type=" + reference.getReferenceType() + " function=" +
                    (caller == null ? "unresolved" : caller.getEntryPoint() + " " + caller.getName()));
                ++count;
            }
            println("incoming_ref_count_reported=" + count);

            DecompInterface decompiler = new DecompInterface();
            try {
                if (decompiler.openProgram(currentProgram)) {
                    DecompileResults result = decompiler.decompileFunction(function, 20, monitor);
                    println("PSEUDOCODE completed=" + result.decompileCompleted());
                    if (result.decompileCompleted()) println(result.getDecompiledFunction().getC());
                }
            } finally { decompiler.dispose(); }

            InstructionIterator calls = currentProgram.getListing().getInstructions(function.getBody(), true);
            while (calls.hasNext()) {
                Instruction instruction = calls.next();
                if (!instruction.getFlowType().isCall()) continue;
                for (Reference reference : currentProgram.getReferenceManager().getReferencesFrom(instruction.getAddress())) {
                    if (!reference.getReferenceType().isCall()) continue;
                    Function callee = getFunctionContaining(reference.getToAddress());
                    println("CALLEE call=" + instruction.getAddress() + " target=" + reference.getToAddress() +
                        " function=" + (callee == null ? "unresolved" : callee.getEntryPoint() + " " + callee.getName()));
                }
            }
        }

        Address start = pc.subtract(0x40);
        Address end = pc.add(0x40);
        Instruction instruction = currentProgram.getListing().getInstructionAt(start);
        if (instruction == null) instruction = currentProgram.getListing().getInstructionAfter(start);
        int count = 0;
        while (instruction != null && instruction.getAddress().compareTo(end) <= 0 && count++ < 64) {
            println("ASM " + instruction.getAddress() + " " + instruction);
            instruction = instruction.getNext();
        }
        if (count == 0) println("assembly=unavailable_in_saved_database");
    }
}
