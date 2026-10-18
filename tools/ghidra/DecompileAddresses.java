// Decompile selected ELF VAs from the read-only 1.26.50.4 project. Evidence only.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class DecompileAddresses extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        if (!di.openProgram(currentProgram)) {
            println("DECOMPILER_OPEN_FAILED");
            return;
        }
        println("IMAGE_BASE " + currentProgram.getImageBase() + " functions=" +
            currentProgram.getFunctionManager().getFunctionCount());
        for (String a : getScriptArgs()) {
            long va = Long.decode(a);
            ghidra.program.model.address.Address addr = currentProgram.getImageBase().add(va);
            Function f = getFunctionAt(addr);
            if (f == null) {
                f = getFunctionContaining(addr);
            }
            println("===== VA " + a + " addr=" + addr + " fn=" + (f == null ? "<none>" : f.getName() + " @ " + f.getEntryPoint()));
            if (f == null) {
                continue;
            }
            DecompileResults r = di.decompileFunction(f, 180, monitor);
            if (!r.decompileCompleted()) {
                println("DECOMPILE_FAILED " + r.getErrorMessage());
                continue;
            }
            println(r.getDecompiledFunction().getC());
        }
        di.dispose();
    }
}
