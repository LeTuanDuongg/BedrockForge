//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileBlockRegistrationCandidate126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final long ELF_START = 0x10487d40L;
    private static final long ELF_END = 0x1048f1b0L;

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF and output path");
        long bias = currentProgram.getImageBase().getOffset();
        Address start = toAddr(bias + ELF_START);
        Address end = toAddr(bias + ELF_END);
        MemoryBlock block = currentProgram.getMemory().getBlock(start);
        if (block == null || !block.isExecute() || !block.contains(end.subtract(1)))
            throw new IllegalStateException("Candidate FDE is not within one executable memory block");
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase());
            out.println("elf_fde=[0x" + Long.toHexString(ELF_START) + ",0x" + Long.toHexString(ELF_END) + ") executable=true");
            DisassembleCommand command = new DisassembleCommand(start, new AddressSet(start, end.subtract(1)), true);
            if (!command.applyTo(currentProgram, monitor))
                throw new IllegalStateException("Bounded disassembly failed: " + command.getStatusMsg());
            Function function = getFunctionAt(start);
            if (function == null) function = createFunction(start, "candidate_block_definition_pipeline_126504");
            if (function == null) throw new IllegalStateException("Function creation failed");
            out.println("function=" + function.getEntryPoint() + " body_bytes=" + function.getBody().getNumAddresses());
            DecompInterface decompiler = new DecompInterface();
            try {
                if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
                DecompileResults result = decompiler.decompileFunction(function, 90, monitor);
                out.println("decompile_completed=" + result.decompileCompleted());
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("decompile_error=" + result.getErrorMessage());
            } finally { decompiler.dispose(); }
        }
    }
}
