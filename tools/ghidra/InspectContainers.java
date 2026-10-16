// Read-only inspection of a copied Ghidra Program; never imports or patches the engine.
//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InspectContainers extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected output path");
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("program=" + currentProgram.getName());
            out.println("sha256=" + currentProgram.getExecutableSHA256());
            out.println("language=" + currentProgram.getLanguageID());
            out.println("functions=" + currentProgram.getFunctionManager().getFunctionCount());
            DecompInterface decompiler = new DecompInterface();
            try {
                if (!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
                String[] terms = {"Inventory", "Container", "ChestScreen", "ScreenController"};
                int[] counts = new int[terms.length];
                int decompiled = 0;
                FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
                while (functions.hasNext() && !monitor.isCancelled()) {
                    Function f = functions.next();
                    String name = f.getName(true);
                    for (int i=0; i<terms.length; i++) {
                        if (!name.contains(terms[i])) continue;
                        counts[i]++;
                        if (counts[i] <= 80) out.println(terms[i] + " " + f.getEntryPoint() + " " + name);
                    }
                    if (decompiled < 8 && (name.contains("ChestScreenController") ||
                        name.contains("ContainerScreenController")) && !f.isExternal() && !f.isThunk()) {
                        var result = decompiler.decompileFunction(f, 45, monitor);
                        out.println("DECOMPILE " + f.getEntryPoint() + " " + name);
                        out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                        decompiled++;
                    }
                }
                for (int i=0; i<terms.length; i++) out.println("count." + terms[i] + "=" + counts[i]);
            } finally { decompiler.dispose(); }
        }
    }
}
