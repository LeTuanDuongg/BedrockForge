//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class TraceContainerStrings extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected output path");
        String[] terms = {"21ChestScreenController", "25ContainerScreenController",
            "ContainerManagerModel.cpp", "InventoryTransaction.cpp", "ChestBlockActor.cpp",
            "container_items", "inventory_size", "container_title"};
        Set<String> seen = new HashSet<>();
        int[] counts = new int[terms.length];
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            out.println("sha256=" + currentProgram.getExecutableSHA256());
            out.println("imageBase=" + currentProgram.getImageBase());
            DecompInterface dec = new DecompInterface();
            try {
                if (!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
                DataIterator data = currentProgram.getListing().getDefinedData(true);
                int decompiled = 0;
                while (data.hasNext() && !monitor.isCancelled()) {
                    Data d = data.next();
                    if (!d.hasStringValue()) continue;
                    String text = String.valueOf(d.getValue());
                    for (int i=0; i<terms.length; i++) {
                        if (!text.contains(terms[i])) continue;
                        counts[i]++;
                        if (counts[i] > 12) continue;
                        out.println("STRING " + d.getAddress() + " " + text);
                        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                        int count=0;
                        while (refs.hasNext() && count++<12) {
                            Reference ref = refs.next();
                            Function f = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                            out.println("REF " + ref.getFromAddress() + " " + ref.getReferenceType() + " " + (f==null ? "no_function" : f.getEntryPoint()+" "+f.getName()));
                            if (f != null && decompiled < 12 && seen.add(f.getEntryPoint().toString())) {
                                var result = dec.decompileFunction(f, 30, monitor);
                                out.println("DECOMPILE " + f.getEntryPoint());
                                out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                                decompiled++;
                            }
                        }
                    }
                }
                for(int i=0;i<terms.length;i++) out.println("count."+terms[i]+"="+counts[i]);
            } finally { dec.dispose(); }
        }
    }
}
