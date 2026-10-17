//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashSet;
import java.util.Set;

public class TraceRegistryGlobalRefs126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final long[] TARGETS = {
        0x134d7000L, 0x134d7db8L, 0x134d7dc0L, 0x134d7dc8L,
        0x134d7dd0L, 0x134d7ee8L, 0x134d7f00L, 0x134d7de8L
    };

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact Minecraft 1.26.50.4 ELF and output path");
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(args[0])))) {
            out.println("sha256=" + SHA256 + " image_base=" + currentProgram.getImageBase());
            out.println("read_only=1;instruction_or_xref_queries_only=1");
            Set<String> seen = new LinkedHashSet<>();
            for (long elfVa : TARGETS) {
                Address target = currentProgram.getImageBase().add(elfVa);
                out.println("\nTARGET elf_va=0x" + Long.toHexString(elfVa) + " ghidra=" + target);
                var refs = currentProgram.getReferenceManager().getReferencesTo(target);
                int count = 0;
                while (refs.hasNext() && count < 512) {
                    Reference ref = refs.next();
                    Instruction hit = getInstructionAt(ref.getFromAddress());
                    Function fn = getFunctionContaining(ref.getFromAddress());
                    String key = ref.getFromAddress().toString();
                    if (!seen.add(key)) continue;
                    ++count;
                    out.println("REF from=" + ref.getFromAddress() + " type=" + ref.getReferenceType() +
                        " instruction=" + hit + " function=" +
                        (fn == null ? "unknown" : fn.getEntryPoint() + "/" + fn.getName()));
                    if (hit == null) continue;
                    Instruction first = hit;
                    for (int i = 0; i < 3 && first.getPrevious() != null; ++i) first = first.getPrevious();
                    for (int i = 0; i < 10 && first != null; ++i, first = first.getNext())
                        out.println("  ASM " + first.getAddress() + " " + first);
                }
                out.println("refs_seen=" + count);
            }
            out.println("\nADRP_PAGE_SCAN page_elf_va=0x134d7000");
            InstructionIterator code = currentProgram.getListing().getInstructions(
                currentProgram.getMemory().getExecuteSet(), true);
            long scanned = 0;
            int matches = 0;
            while (code.hasNext()) {
                Instruction ins = code.next();
                ++scanned;
                if (!"ADRP".equalsIgnoreCase(ins.getMnemonicString())) continue;
                String text = ins.toString().toLowerCase();
                if (!text.contains("134d7000") && !text.contains("135d7000")) continue;
                Function fn = getFunctionContaining(ins.getAddress());
                out.println("ADRP from=" + ins.getAddress() + " function=" +
                    (fn == null ? "unknown" : fn.getEntryPoint() + "/" + fn.getName()));
                Instruction next = ins;
                for (int i = 0; i < 8 && next != null; ++i, next = next.getNext())
                    out.println("  ASM " + next.getAddress() + " " + next);
                if (++matches >= 256) break;
            }
            out.println("instructions_scanned=" + scanned + " adrp_matches=" + matches);
        }
    }
}
