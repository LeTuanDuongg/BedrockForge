//@category HybridMCPE
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class TraceBlockRegistrationOrchestrator126504 extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private final Set<String> reported = new HashSet<>();

    @Override public void run() throws Exception {
        String databaseSha = currentProgram.getExecutableSHA256();
        if (databaseSha != null && !databaseSha.isEmpty() && !SHA256.equals(databaseSha))
            throw new IllegalArgumentException("Ghidra database SHA-256 does not match target ELF: " + databaseSha);
        println("target_sha256=" + SHA256 + " database_sha256=" +
            (databaseSha == null || databaseSha.isEmpty() ? "unavailable" : databaseSha) +
            " executable_path=" + currentProgram.getExecutablePath() +
            " language=" + currentProgram.getLanguageID() + " image_base=" + currentProgram.getImageBase());
        long[] anchors = {0x93c3e90L, 0x93c3f00L, 0x94f679cL, 0x94f690cL,
            0x94f6ac8L, 0x100fb840L, 0x101071e0L, 0x101f95a0L, 0x93c3cbcL};
        for (long value : anchors) inspect(toAddr(value));
    }

    private void inspect(Address anchor) {
        Instruction instruction = getInstructionAt(anchor);
        Function function = getFunctionContaining(anchor);
        println("ANCHOR va=" + anchor + " instruction=" +
            (instruction == null ? "unavailable" : instruction.toString()) +
            " function=" + (function == null ? "unresolved" : function.getEntryPoint() + ":" +
                function.getName() + ":bytes=" + function.getBody().getNumAddresses()));
        if (function != null && reported.add(function.getEntryPoint().toString())) callers(function);

        Address start = anchor.subtract(0x30), end = anchor.add(0x30);
        Instruction cursor = currentProgram.getListing().getInstructionAt(start);
        if (cursor == null) cursor = currentProgram.getListing().getInstructionAfter(start);
        int count = 0;
        while (cursor != null && cursor.getAddress().compareTo(end) <= 0 && count++ < 28) {
            if (cursor.getFlowType().isCall()) {
                Address[] flows = cursor.getFlows();
                for (Address target : flows) {
                    Function callee = getFunctionContaining(target);
                    println("CALL pc=" + cursor.getAddress() + " target=" + target + " callee=" +
                        (callee == null ? "unresolved" : callee.getEntryPoint() + ":" + callee.getName()));
                }
            }
            cursor = cursor.getNext();
        }
    }

    private void callers(Function function) {
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
        int count = 0;
        while (refs.hasNext() && count < 40) {
            Reference ref = refs.next();
            if (!ref.getReferenceType().isCall()) continue;
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("CALLER entry=" + (caller == null ? "unresolved" : caller.getEntryPoint()) +
                " name=" + (caller == null ? "unresolved" : caller.getName()) +
                " callsite=" + ref.getFromAddress());
            ++count;
        }
        println("CALLERS_REPORTED function=" + function.getEntryPoint() + " count=" + count);
    }
}
