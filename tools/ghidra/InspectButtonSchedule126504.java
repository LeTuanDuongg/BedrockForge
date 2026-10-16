//@category HybridMCPE
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.AddressSet;
import java.io.PrintWriter;

public class InspectButtonSchedule126504 extends GhidraScript {
    public void run() throws Exception {
        if (!"fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1".equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong engine");
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            DecompInterface d = new DecompInterface();
            try {
                // The dynamic PLT symbol is verified by LLVM; its missing noreturn flag caused fallthrough.
                var fail = getFunctionAt(currentProgram.getImageBase().add(0x1291d400L));
                if (fail != null) fail.setNoReturn(true);
                d.openProgram(currentProgram);
                for (long va : new long[]{0x101147a8L, 0xfb8edc8L}) {
                    var address = currentProgram.getImageBase().add(va);
                    Function f = getFunctionAt(address);
                    if (f == null) { disassemble(address); f = createFunction(address, null); }
                    out.println("candidate=0x" + Long.toHexString(va) + " database=" + address + " function=" + f);
                    out.println("first_word=" + Integer.toHexString(getInt(address)));
                    if (f == null) continue;
                    f.setBody(new AddressSet(address, address.add(va == 0xfb8edc8L ? 0xdf : 0x603)));
                    d.flushCache();
                    out.println("body=" + f.getBody());
                    var result = d.decompileFunction(f, 20, monitor);
                    out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                }
            } finally { d.dispose(); }
        }
    }
}
