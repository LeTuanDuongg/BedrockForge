//@category HybridMCPE
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;

public class AuditTarget126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final long[][] RANGES = {
        {0x103e71acL, 0x103e7220L},
        {0x10487d40L, 0x1048f1b0L},
        {0x1048f1b0L, 0x1048f3d4L},
        {0x1048f3d4L, 0x1048f5dcL}
    };

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF and output path");

        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(args[0])))) {
            out.println("sha256=" + currentProgram.getExecutableSHA256());
            out.println("language=" + currentProgram.getLanguageID() + " image_base=" + currentProgram.getImageBase());
            out.println("scope=4 unwind-bounded ranges; no full Auto Analysis");

            DecompInterface decompiler = new DecompInterface();
            try {
                if (!decompiler.openProgram(currentProgram))
                    throw new IllegalStateException(decompiler.getLastMessage());

                for (long[] range : RANGES) {
                    monitor.checkCancelled();
                    Address start = currentProgram.getImageBase().add(range[0]);
                    Address end = currentProgram.getImageBase().add(range[1]);
                    MemoryBlock block = currentProgram.getMemory().getBlock(start);
                    if (block == null || !block.isExecute() || !block.contains(end.subtract(1))) {
                        out.println("range=0x" + Long.toHexString(range[0]) + "..0x" + Long.toHexString(range[1]) +
                            " status=not-contained-in-executable-block");
                        continue;
                    }

                    AddressSet body = new AddressSet(start, end.subtract(1));
                    DisassembleCommand command = new DisassembleCommand(start, body, true);
                    if (!command.applyTo(currentProgram, monitor)) {
                        out.println("range=0x" + Long.toHexString(range[0]) + "..0x" + Long.toHexString(range[1]) +
                            " status=disassembly-failed reason=" + command.getStatusMsg());
                        continue;
                    }

                    Function function = getFunctionAt(start);
                    if (function == null)
                        function = currentProgram.getFunctionManager().createFunction(
                            "audit_" + Long.toHexString(range[0]), start, body, SourceType.ANALYSIS);
                    if (function == null || !function.getEntryPoint().equals(start)) {
                        out.println("range=0x" + Long.toHexString(range[0]) + "..0x" + Long.toHexString(range[1]) +
                            " status=function-boundary-conflict");
                        continue;
                    }

                    out.println("\nfunction=" + function.getEntryPoint() + " name=" + function.getName() +
                        " fde_bytes=" + (range[1] - range[0]) + " body_bytes=" + function.getBody().getNumAddresses() +
                        " existing_signature=" + function.getSignature());
                    long instructions = 0;
                    InstructionIterator iterator = currentProgram.getListing().getInstructions(body, true);
                    while (iterator.hasNext()) { iterator.next(); instructions++; }
                    out.println("instruction_count=" + instructions);

                    DecompileResults result = decompiler.decompileFunction(function, 20, monitor);
                    out.println("decompile_completed=" + result.decompileCompleted());
                    if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                    else out.println("decompile_error=" + result.getErrorMessage());
                }
            } finally {
                decompiler.dispose();
            }
        }
    }
}
