//@category HybridMCPE
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.io.BufferedReader;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class ImportFdeBatch extends GhidraScript {
    private static final String SHA256 = "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1";
    private static final int MAX_BATCH = 64;
    private static final int MAX_DECOMPILES = 16;
    private static final long MAX_DECOMPILE_BYTES = 4096;

    private static final class Row {
        final long start, end, fdeFileOffset;
        Row(long start, long end, long fdeFileOffset) {
            this.start = start; this.end = end; this.fdeFileOffset = fdeFileOffset;
        }
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 4 || !SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalArgumentException("Expected exact 1.26.50.4 ELF and TSV, offset, count, report arguments");
        Path input = Path.of(args[0]);
        int offset = Integer.parseInt(args[1]);
        int count = Integer.parseInt(args[2]);
        Path report = Path.of(args[3]);
        if (offset < 0 || count < 1 || count > MAX_BATCH)
            throw new IllegalArgumentException("Batch must contain 1.." + MAX_BATCH + " rows");

        List<Row> rows = readRows(input);
        if (offset >= rows.size()) throw new IllegalArgumentException("Batch offset beyond TSV row count");
        int limit = Math.min(rows.size(), offset + count);
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(report))) {
            out.println("engine_sha256=" + SHA256);
            out.println("program=" + currentProgram.getName() + " language=" + currentProgram.getLanguageID() +
                " image_base=" + currentProgram.getImageBase());
            out.println("batch_offset=" + offset + " requested=" + count + " selected=" + (limit - offset));
            DecompInterface decompiler = new DecompInterface();
            int decompiled = 0, created = 0, reused = 0, skipped = 0, calls = 0;
            try {
                if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
                for (int i = offset; i < limit; i++) {
                    monitor.checkCancelled();
                    Row row = rows.get(i);
                    Address start = currentProgram.getImageBase().add(row.start);
                    Address end = currentProgram.getImageBase().add(row.end);
                    Address last = end.subtract(1);
                    MemoryBlock block = currentProgram.getMemory().getBlock(start);
                    if (row.start >= row.end || block == null || !block.isExecute() || !block.contains(last)) {
                        out.println("row=" + i + " status=reject_non_executable_or_invalid_range"); skipped++; continue;
                    }
                    AddressSet body = new AddressSet(start, last);
                    DisassembleCommand command = new DisassembleCommand(start, body, true);
                    if (!command.applyTo(currentProgram, monitor)) {
                        out.println("row=" + i + " start=" + start + " status=disassembly_failed reason=" + command.getStatusMsg());
                        skipped++; continue;
                    }

                    Function function = getFunctionAt(start);
                    if (function == null && getFunctionContaining(start) != null) {
                        out.println("row=" + i + " start=" + start + " status=boundary_conflict");
                        skipped++; continue;
                    }
                    if (function == null) {
                        function = currentProgram.getFunctionManager().createFunction(
                            "fde_va_" + Long.toHexString(row.start), start, body, SourceType.ANALYSIS);
                        if (function == null) {
                            out.println("row=" + i + " start=" + start + " status=function_creation_failed");
                            skipped++; continue;
                        }
                        created++;
                    } else {
                        reused++;
                    }
                    if (function.getComment() == null || function.getComment().isBlank()) {
                        function.setComment("FDE candidate for Minecraft 1.26.50.4 ARM64; ELF VA range 0x" +
                            Long.toHexString(row.start) + "..0x" + Long.toHexString(row.end) +
                            "; engine SHA-256 " + SHA256 + ". FDE boundary does not prove a unique C++ function, ABI, or semantics.");
                    } else {
                        out.println("existing_plate_comment_preserved entry=" + function.getEntryPoint());
                    }

                    long instructionCount = 0;
                    InstructionIterator iterator = currentProgram.getListing().getInstructions(function.getBody(), true);
                    while (iterator.hasNext()) {
                        Instruction instruction = iterator.next();
                        instructionCount++;
                        if (instruction.getFlowType().isCall()) {
                            for (Reference reference : currentProgram.getReferenceManager().getReferencesFrom(instruction.getAddress())) {
                                if (reference.getReferenceType().isCall()) {
                                    calls++;
                                    out.println("call from=" + instruction.getAddress() + " to=" + reference.getToAddress());
                                    if (instruction.getAddress().equals(currentProgram.getImageBase().add(0xfb3c1acL)))
                                        out.println("known_callsite_0xfb3c1ac target=" + reference.getToAddress());
                                }
                            }
                        }
                    }
                    out.println("row=" + i + " status=" + (created + reused > 0 ? "function_ready" : "unknown") +
                        " elf_va=0x" + Long.toHexString(row.start) + "..0x" + Long.toHexString(row.end) +
                        " fde_file_offset=0x" + Long.toHexString(row.fdeFileOffset) + " entry=" + function.getEntryPoint() +
                        " bytes=" + (row.end - row.start) + " instructions=" + instructionCount +
                        " name=" + function.getName() + " signature_inferred=" + function.getSignature());

                    if (decompiled < MAX_DECOMPILES && row.end - row.start <= MAX_DECOMPILE_BYTES) {
                        DecompileResults result = decompiler.decompileFunction(function, 15, monitor);
                        out.println("decompile entry=" + function.getEntryPoint() + " completed=" + result.decompileCompleted());
                        if (result.decompileCompleted()) {
                            out.println("decompile_signature=" + result.getDecompiledFunction().getSignature());
                            decompiled++;
                        } else out.println("decompile_error=" + result.getErrorMessage());
                    } else {
                        out.println("decompile_skipped entry=" + function.getEntryPoint() + " bytes=" + (row.end - row.start));
                    }
                }
            } finally {
                decompiler.dispose();
            }
            int totalFunctions = currentProgram.getFunctionManager().getFunctionCount();
            out.println("batch_result created=" + created + " reused=" + reused + " decompiled=" + decompiled +
                " call_references=" + calls + " skipped=" + skipped + " total_functions=" + totalFunctions +
                " save=HeadlessAnalyzer");
        }
    }

    private List<Row> readRows(Path input) throws Exception {
        List<Row> rows = new ArrayList<>();
        String headerHash = null;
        long previousEnd = -1;
        try (BufferedReader reader = Files.newBufferedReader(input)) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.startsWith("#")) {
                    for (String field : line.substring(1).trim().split(" "))
                        if (field.startsWith("sha256=")) headerHash = field.substring(7);
                    continue;
                }
                if (line.isBlank()) continue;
                String[] columns = line.split("\t");
                if (columns.length != 3) throw new IllegalArgumentException("Malformed TSV row: " + line);
                long start = Long.decode(columns[0]), end = Long.decode(columns[1]), fdeOffset = Long.decode(columns[2]);
                if (start >= end || start < previousEnd) throw new IllegalArgumentException("Unsorted or overlapping TSV ranges");
                previousEnd = end;
                rows.add(new Row(start, end, fdeOffset));
            }
        }
        if (!SHA256.equals(headerHash)) throw new IllegalArgumentException("FDE TSV engine hash mismatch");
        return rows;
    }
}
