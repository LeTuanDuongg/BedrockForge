//@category HybridMCPE
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;

public class DumpBlockRegistryFlow126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final long[] FUNCTIONS = {
        0xb1df98cL, 0x103e285cL, 0x103e286cL, 0x103e2b08L,
        0x103e2d54L, 0x103e2d60L, 0x103e3cd0L, 0x103e5648L,
        0x103e5cacL, 0x103e714cL, 0x10481320L, 0x1048135cL
    };
    private static final long[] WINDOWS = {
        0x1048c514L, 0x1048c520L, 0x1048c580L, 0x1048f518L
    };
    private static final long[] XREF_TARGETS = {
        0x103e2d54L, 0x103e2d60L, 0x103e285cL, 0x103e5648L,
        0x103e5cacL, 0x103e3cd0L, 0x10481320L, 0x1048135cL,
        0x10487d40L, 0xb1df98cL
    };

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF and output path");
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(args[0])))) {
            out.println("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase() +
                " language=" + currentProgram.getLanguageID());
            out.println("Read-only Ghidra inspection. No engine call or database mutation.");
            DecompInterface decompiler = new DecompInterface();
            try {
                if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
                for (long elfVa : FUNCTIONS) dumpFunction(elfVa, decompiler, out);
                for (long elfVa : WINDOWS) dumpWindow(elfVa, out);
                for (long elfVa : XREF_TARGETS) dumpIncoming(elfVa, out);
            } finally { decompiler.dispose(); }
        }
    }

    private void dumpFunction(long elfVa, DecompInterface decompiler, PrintWriter out) {
        Address entry = currentProgram.getImageBase().add(elfVa);
        Function function = getFunctionAt(entry);
        out.println("\nFUNCTION elf_va=0x" + Long.toHexString(elfVa) + " ghidra=" + entry +
            " exists=" + (function != null));
        if (function == null) return;
        long bytes = function.getBody().getNumAddresses();
        out.println("name=" + function.getName() + " bytes=" + bytes + " signature_inferred=" + function.getSignature() +
            " calling_convention=" + function.getCallingConventionName() + " parameters=" + function.getParameterCount());

        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            if (!instruction.getFlowType().isCall()) continue;
            for (Reference ref : currentProgram.getReferenceManager().getReferencesFrom(instruction.getAddress())) {
                if (ref.getReferenceType().isCall())
                    out.println("CALL from=" + instruction.getAddress() + " elf_va=0x" +
                        Long.toHexString(instruction.getAddress().subtract(currentProgram.getImageBase())) +
                        " instruction=" + instruction + " type=" + ref.getReferenceType() + " to=" + ref.getToAddress());
            }
        }

        if (bytes > 8192) {
            out.println("DECOMPILE skipped: selected-region review required");
            return;
        }
        DecompileResults result = decompiler.decompileFunction(function, 20, monitor);
        out.println("DECOMPILE completed=" + result.decompileCompleted() + " error=" + result.getErrorMessage());
        if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
    }

    private void dumpWindow(long elfVa, PrintWriter out) {
        Address center = currentProgram.getImageBase().add(elfVa);
        Instruction instruction = getInstructionAt(center);
        out.println("\nWINDOW elf_va=0x" + Long.toHexString(elfVa) + " ghidra=" + center + " exists=" + (instruction != null));
        if (instruction == null) return;
        for (int i = 0; i < 8 && instruction.getPrevious() != null; i++) instruction = instruction.getPrevious();
        for (int i = 0; i < 20 && instruction != null; i++, instruction = instruction.getNext()) {
            out.println("ASM " + instruction.getAddress() + " elf_va=0x" +
                Long.toHexString(instruction.getAddress().subtract(currentProgram.getImageBase())) + " " + instruction);
        }
    }

    private void dumpIncoming(long elfVa, PrintWriter out) {
        Address target = currentProgram.getImageBase().add(elfVa);
        out.println("\nINCOMING elf_va=0x" + Long.toHexString(elfVa) + " ghidra=" + target);
        var references = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (references.hasNext()) {
            Reference reference = references.next();
            Function caller = getFunctionContaining(reference.getFromAddress());
            out.println("XREF from=" + reference.getFromAddress() + " type=" + reference.getReferenceType() +
                " caller=" + (caller == null ? "unknown" : caller.getEntryPoint() + "/" + caller.getName()));
            if (++count >= 128) { out.println("XREF truncated_at=128"); break; }
        }
        out.println("XREF count_seen=" + count);
    }
}
