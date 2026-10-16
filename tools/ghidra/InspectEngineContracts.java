//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.Address;
import java.io.*;
import java.nio.file.*;

public class InspectEngineContracts extends GhidraScript {
    private static final String TARGET_SHA256 = "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a";

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected profile seeds and output path");
        if (!TARGET_SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong engine database");

        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[1]))) {
            if (!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
            out.println("target_sha256=" + currentProgram.getExecutableSHA256());
            out.println("ghidra_image_base=" + currentProgram.getImageBase());
            out.println("Evidence is static. Ghidra signatures are inferred and do not establish a safe callable ABI.");

            for (String line : Files.readAllLines(Path.of(args[0]))) {
                if (line.isBlank() || monitor.isCancelled()) continue;
                String[] seed = line.trim().split("\\s+");
                if (seed.length != 2) throw new IllegalArgumentException("Bad seed: " + line);
                Address address = currentProgram.getImageBase().add(Long.decode(seed[1]));
                Function function = getFunctionAt(address);
                if (function == null) function = getFunctionContaining(address);
                out.println("\nCANDIDATE " + seed[0] + " ELF_VA=" + seed[1] + " GHIDRA=" + address);
                if (function == null) {
                    out.println("NO_FUNCTION boundary not present in copied analysis database");
                    continue;
                }

                out.println("function=" + function.getName() + " entry=" + function.getEntryPoint() + " end=" + function.getBody().getMaxAddress());
                out.println("database_return_type=" + function.getReturnType().getDisplayName());
                out.println("database_calling_convention=" + function.getCallingConventionName());
                out.println("database_parameters=" + function.getParameterCount());
                for (Parameter parameter : function.getParameters())
                    out.println("parameter " + parameter.getOrdinal() + " " + parameter.getDataType().getDisplayName() + " " + parameter.getName());

                int referenceCount = 0;
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    referenceCount++;
                    if (referenceCount <= 24) {
                        Function caller = getFunctionContaining(reference.getFromAddress());
                        out.println("xref " + reference.getReferenceType() + " " + reference.getFromAddress() + " " +
                            (caller == null ? "no_function" : caller.getEntryPoint() + " " + caller.getName()));
                    }
                }
                out.println("xref_count=" + referenceCount);

                int instructionCount = 0;
                InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext() && instructionCount < 32) {
                    Instruction instruction = instructions.next();
                    out.println("asm " + instruction.getAddress() + " " + instruction);
                    instructionCount++;
                }

                var result = decompiler.decompileFunction(function, 30, monitor);
                out.println("decompile=" + (result.decompileCompleted() ? "complete" : result.getErrorMessage()));
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
