// Writes a flat text listing of the hunk blocks (CODE_*) to the path given as the first argument.
// Instructions show bytes, mnemonic and operands; undefined/data runs show bytes (16 per line).
// Labels carry their incoming-reference sources so the listing can be read offline.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class DumpListing extends GhidraScript {
    private static String hex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) sb.append(String.format("%02x", b & 0xff));
        return sb.toString();
    }

    private String refsTo(Address a) {
        ReferenceManager rm = currentProgram.getReferenceManager();
        List<String> out = new ArrayList<>();
        for (Reference r : rm.getReferencesTo(a)) {
            String t = r.getReferenceType().isCall() ? "c" : r.getReferenceType().isJump() ? "j" : r.getReferenceType().isRead() ? "r" : r.getReferenceType().isWrite() ? "w" : "d";
            out.add(t + r.getFromAddress().toString());
            if (out.size() >= 24) { out.add("..."); break; }
        }
        return String.join(" ", out);
    }

    private void label(PrintWriter w, Address a) {
        Symbol[] syms = currentProgram.getSymbolTable().getSymbols(a);
        Function f = getFunctionAt(a);
        if (f != null) w.printf("%n; ======== FUNCTION %s%n", f.getName());
        for (Symbol s : syms) {
            if (s.getSymbolType() == SymbolType.FUNCTION) continue;
            w.printf("%s:%n", s.getName());
        }
        if (syms.length > 0 || f != null) {
            String r = refsTo(a);
            if (!r.isEmpty()) w.printf("    ; xref %s%n", r);
        }
        String plate = currentProgram.getListing().getComment(CodeUnit.PLATE_COMMENT, a);
        if (plate != null) for (String line : plate.split("\n")) w.printf("    ;; %s%n", line);
    }

    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(path)) {
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.getName().startsWith("CODE_")) continue;
                w.printf("%n;;;;;;;; BLOCK %s %s-%s%n", b.getName(), b.getStart(), b.getEnd());
                CodeUnitIterator it = listing.getCodeUnits(b.getStart(), true);
                StringBuilder run = new StringBuilder();
                Address runStart = null;
                int runLen = 0;
                while (it.hasNext()) {
                    CodeUnit cu = it.next();
                    Address a = cu.getAddress();
                    if (a.compareTo(b.getEnd()) > 0) break;
                    boolean undef = cu instanceof Data && !((Data) cu).isDefined();
                    boolean hasSym = currentProgram.getSymbolTable().getSymbols(a).length > 0 || getFunctionAt(a) != null;
                    if (runLen > 0 && (!undef || hasSym || runLen == 16)) {
                        w.printf("%s  dc.b  %s%n", runStart, run);
                        run.setLength(0);
                        runLen = 0;
                    }
                    label(w, a);
                    String eol = cu.getComment(CodeUnit.EOL_COMMENT);
                    String pre = cu.getComment(CodeUnit.PRE_COMMENT);
                    if (pre != null) for (String line : pre.split("\n")) w.printf("    ; %s%n", line);
                    if (undef) {
                        if (runLen == 0) runStart = a;
                        run.append(String.format("%02x", cu.getBytes()[0] & 0xff));
                        runLen++;
                        continue;
                    }
                    if (cu instanceof Instruction) {
                        Instruction ins = (Instruction) cu;
                        StringBuilder refs = new StringBuilder();
                        for (Reference r : ins.getReferencesFrom()) {
                            if (r.getReferenceType().isFlow() || r.isMemoryReference()) {
                                Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(r.getToAddress());
                                refs.append(" ->").append(s != null ? s.getName() : r.getToAddress().toString());
                            }
                        }
                        w.printf("%s  %-20s %-40s%s%s%n", a, hex(ins.getBytes()), ins.toString(),
                                refs.length() > 0 ? " ;" + refs : "", eol != null ? " ; " + eol : "");
                    } else {
                        Data d = (Data) cu;
                        byte[] bytes = d.getBytes();
                        String shown = bytes.length > 32 ? hex(java.util.Arrays.copyOf(bytes, 32)) + "..." : hex(bytes);
                        w.printf("%s  data %s [%d] %s %s%s%n", a, d.getDataType().getName(), bytes.length, shown,
                                d.getDefaultValueRepresentation(), eol != null ? " ; " + eol : "");
                    }
                }
                if (runLen > 0) w.printf("%s  dc.b  %s%n", runStart, run);
            }
        }
        println("wrote " + path);
    }
}
