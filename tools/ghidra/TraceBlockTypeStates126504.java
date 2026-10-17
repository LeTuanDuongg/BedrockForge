import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;

/** Query existing incoming references only; no ELF scan or Auto Analysis. */
public class TraceBlockTypeStates126504 extends GhidraScript {
    public void run() throws Exception {
        if (!"fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1"
                .equals(currentProgram.getExecutableSHA256()) || getScriptArgs().length != 1)
            throw new IllegalArgumentException("Exact 1.26.50.4 engine and output path required");
        long base = currentProgram.getImageBase().getOffset();
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(getScriptArgs()[0])))) {
            out.println("Existing database references; missing references do not prove no caller.");
            for (long va : new long[]{0x1010d108L, 0xfb8bb48L, 0x1010c57cL, 0x101fc8c8L}) {
                Address target = toAddr(base + va);
                out.printf("TARGET elf_va=0x%x%n", va);
                int count = 0;
                for (Reference ref : getReferencesTo(target)) {
                    if (++count > 24) { out.println("references_truncated=true"); break; }
                    Address from = ref.getFromAddress();
                    Function function = getFunctionContaining(from);
                    out.printf("REF from_elf_va=0x%x kind=%s function=%s%n",
                        from.getOffset() - base, ref.getReferenceType(),
                        function == null ? "unknown" : "0x" + Long.toHexString(function.getEntryPoint().getOffset() - base));
                    if (ref.getReferenceType().isCall()) {
                        for (int delta = -24; delta <= 12; delta += 4) {
                            Address at = from.add(delta);
                            out.printf("  0x%x %s%n", at.getOffset() - base, getInstructionAt(at));
                        }
                    }
                }
                out.printf("reference_count_bounded=%d%n", Math.min(count, 24));
            }
        }
    }
}
