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
        if (args.length != 3) throw new IllegalArgumentException("Expected function seeds, vtable seeds and output path");
        if (!TARGET_SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong engine database");

        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter out = new PrintWriter(new FileWriter(args[2]))) {
            if (!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
            out.println("target_sha256=" + currentProgram.getExecutableSHA256());
            out.println("ghidra_image_base=" + currentProgram.getImageBase());
            out.println("Evidence is static. Ghidra signatures are inferred and do not establish a safe callable ABI.");

            for (String line : Files.readAllLines(Path.of(args[1]))) {
                if (line.isBlank() || monitor.isCancelled()) continue;
                String[] seed = line.trim().split("\\s+");
                if (seed.length != 3) throw new IllegalArgumentException("Bad vtable seed: " + line);
                for (int i = 1; i < seed.length; ++i) {
                    Address address = currentProgram.getImageBase().add(Long.decode(seed[i]));
                    out.println("\nVTABLE " + seed[0] + (i == 1 ? " address_point=" : " typeinfo=") + address);
                    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
                    int count = 0;
                    while (refs.hasNext()) {
                        Reference ref = refs.next();
                        Function caller = getFunctionContaining(ref.getFromAddress());
                        out.println("vtable_ref " + ref.getReferenceType() + " from=" + ref.getFromAddress() +
                            " " + (caller == null ? "no_function" : caller.getEntryPoint() + " " + caller.getName()));
                        count++;
                    }
                    out.println("vtable_ref_count=" + count);
                }
            }

            for (String line : Files.readAllLines(Path.of(args[0]))) {
                if (line.isBlank() || monitor.isCancelled()) continue;
                String[] seed = line.trim().split("\\s+");
                if (seed.length != 2) throw new IllegalArgumentException("Bad seed: " + line);
                Address address = currentProgram.getImageBase().add(Long.decode(seed[1]));
                Function function = getFunctionAt(address);
                if (function == null) {
                    function = getFunctionContaining(address);
                    if (function == null) {
                        if (currentProgram.getListing().getInstructionAt(address) == null) disassemble(address);
                        function = createFunction(address, null);
                    }
                }
                out.println("\nCANDIDATE " + seed[0] + " ELF_VA=" + seed[1] + " GHIDRA=" + address);
                if (function == null) {
                    out.println("NO_FUNCTION boundary unavailable or overlaps an existing code unit");
                    continue;
                }

                out.println("function=" + function.getName() + " entry=" + function.getEntryPoint() + " end=" + function.getBody().getMaxAddress());
                out.println("boundary=" + (function.getEntryPoint().equals(address) ? "exact" : "candidate_inside_other_function"));
                long bodySize = function.getBody().getMaxAddress().subtract(function.getBody().getMinAddress()) + 1;
                out.println("function_body_bytes=" + bodySize);
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
                        if (seed[0].startsWith("9ItemStack.vtable0.slot0") && reference.getReferenceType().isCall()) {
                            Instruction site = currentProgram.getListing().getInstructionAt(reference.getFromAddress());
                            Instruction first = site;
                            for (int i = 0; i < 4 && first != null && first.getPrevious() != null; ++i) first = first.getPrevious();
                            for (int i = 0; i < 9 && first != null; ++i, first = first.getNext())
                                out.println("itemstack_dtor_call_context " + first.getAddress() + " " + first);
                        }
                    }
                }
                out.println("xref_count=" + referenceCount);

                int instructionCount = 0;
                int callCount = 0;
                boolean boundedBody = function.getEntryPoint().equals(address) && bodySize <= 0x10000;
                InstructionIterator instructions = boundedBody ? currentProgram.getListing().getInstructions(function.getBody(), true) :
                    currentProgram.getListing().getInstructions(address, true);
                while (instructions.hasNext() && instructionCount < 48) {
                    Instruction instruction = instructions.next();
                    if (instruction.getAddress().compareTo(address) < 0) continue;
                    if (!boundedBody && instruction.getAddress().subtract(address) > 0x100) break;
                    out.println("asm " + instruction.getAddress() + " " + instruction);
                    instructionCount++;
                    if (instruction.getFlowType().isCall()) {
                        Address[] flows = instruction.getFlows();
                        if (flows.length == 0) {
                            out.println((boundedBody ? "callee" : "window_call") + " indirect call_site=" + instruction.getAddress() + " " + instruction);
                        } else {
                            for (Address target : flows) {
                                Function callee = getFunctionAt(target);
                                if (callee == null) callee = getFunctionContaining(target);
                                out.println((boundedBody ? "callee" : "window_call") + " direct call_site=" + instruction.getAddress() + " target=" + target +
                                    " " + (callee == null ? "no_function" : callee.getName()));
                            }
                        }
                        callCount++;
                    }
                }
                out.println("call_instruction_count=" + callCount);

                if (function.getEntryPoint().equals(address) && bodySize <= 0x10000) {
                    var result = decompiler.decompileFunction(function, 30, monitor);
                    out.println("decompile=" + (result.decompileCompleted() ? "complete" : result.getErrorMessage()));
                    if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                } else {
                    out.println("decompile=skipped_boundary_mismatch");
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
