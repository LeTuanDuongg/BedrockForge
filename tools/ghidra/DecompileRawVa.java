// Disassemble + decompile arbitrary ELF VAs (image base + VA). Evidence only.
// Args: "startHEX:endHEX" pairs.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class DecompileRawVa extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        if (!di.openProgram(currentProgram)) { println("OPEN_FAILED"); return; }
        println("IMAGE_BASE " + currentProgram.getImageBase());
        for (String a : getScriptArgs()) {
            String[] p = a.split(":");
            long s = Long.decode(p[0]);
            long e = p.length > 1 ? Long.decode(p[1]) : s + 4;
            Address start = currentProgram.getImageBase().add(s);
            Address last = currentProgram.getImageBase().add(e).subtract(1);
            MemoryBlock block = currentProgram.getMemory().getBlock(start);
            if (block == null || !block.contains(last)) {
                println("BAD_RANGE " + a + " block=" + block);
                continue;
            }
            AddressSet body = new AddressSet(start, last);
            DisassembleCommand cmd = new DisassembleCommand(start, body, true);
            boolean ok = cmd.applyTo(currentProgram, monitor);
            Function f = getFunctionAt(start);
            if (f == null) { f = getFunctionContaining(start); }
            if (f == null) {
                f = currentProgram.getFunctionManager().createFunction(
                    "raw_" + Long.toHexString(s), start, body, SourceType.USER_DEFINED);
            }
            println("===== " + a + " disasm=" + ok + " fn=" + (f == null ? "<none>" : f.getEntryPoint()));
            if (f == null) { continue; }
            DecompileResults r = di.decompileFunction(f, 240, monitor);
            if (!r.decompileCompleted()) { println("DECOMPILE_FAILED " + r.getErrorMessage()); continue; }
            println(r.getDecompiledFunction().getC());
        }
        di.dispose();
    }
}
