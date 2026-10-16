//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.io.*;
import java.nio.file.*;

public class DecompileVtableSeeds extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if(args.length!=2) throw new IllegalArgumentException("Expected seeds and output");
        if(!"53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a".equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Wrong engine database");
        DecompInterface dec = new DecompInterface();
        try(PrintWriter out=new PrintWriter(new FileWriter(args[1]))) {
            if(!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
            out.println("Recovered pseudocode; vtable slot meanings and call signatures remain unverified.");
            int count=0;
            for(String line:Files.readAllLines(Path.of(args[0]))) {
                if(line.isBlank() || monitor.isCancelled()) continue;
                String[] seed=line.split(" ");
                long va=Long.decode(seed[1]);
                var address=currentProgram.getImageBase().add(va);
                out.println("SEED "+seed[0]+" ELF_VA="+seed[1]+" GHIDRA="+address);
                Function f=getFunctionAt(address);
                if(f==null) {
                    disassemble(address);
                    f=createFunction(address,null);
                }
                if(f==null) { out.println("UNRESOLVED function creation failed"); continue; }
                var result=dec.decompileFunction(f,30,monitor);
                out.println(result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage());
                if(++count>=24) break;
            }
        } finally { dec.dispose(); }
    }
}
