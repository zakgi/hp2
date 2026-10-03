// Applies a JSON annotation batch to hp.prg in one transaction and prints a read-back.
//
// Batch format (all keys optional). Places are hunk:offset with a hexadecimal offset; a bare
// hexadecimal number is taken as a Ghidra address (older batches).
// {
//   "create_functions": ["0:bd4c", ...],
//   "functions": [{"addr": "1:001c", "name": "BlitBob", "plate": "...", "signature": "void BlitBob(long a)"}],
//   "labels":    [{"addr": "0:0060", "name": "g_drawBuffer", "type": "ptr", "plate": "...", "eol": "..."}],
//   "comments":  [{"addr": "1:0b26", "eol": "...", "pre": "..."}]
// }
// Label types: byte, word, dword, ptr, string, or <base>[n] arrays of byte/word/dword/ptr.
// Function signatures are C prototypes parsed against the program's data types; the
// calling convention stays as it is. Register-argument functions get "void f(void)".
import ghidra.app.script.GhidraScript;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.util.CodeUnitInsertionException;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.data.DataUtilities;

import com.google.gson.*;

import java.nio.file.Files;
import java.nio.file.Path;

public class ApplyAnnotations extends GhidraScript {
    // Where the project loads each hunk of hp.prg (scripts/hp2lib/exe.py, GHIDRA_HUNK_ADDRESSES).
    private static final long[] HUNK_ADDRESSES = {0x0021f000L, 0x0022d8a8L};

    private Address addr(String s) {
        long address;
        int colon = s.indexOf(':');
        if (colon >= 0) {
            address = HUNK_ADDRESSES[Integer.parseInt(s.substring(0, colon))] + Long.parseLong(s.substring(colon + 1), 16);
        } else {
            address = Long.parseLong(s.replace("0x", ""), 16);
        }
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(address);
    }

    private DataType baseType(String t) {
        switch (t) {
            case "byte": return ByteDataType.dataType;
            case "word": return WordDataType.dataType;
            case "sword": return ShortDataType.dataType;
            case "dword": return DWordDataType.dataType;
            case "ptr": return PointerDataType.dataType;
            case "string": return StringDataType.dataType;
            default: throw new IllegalArgumentException("unknown type " + t);
        }
    }

    private DataType typeOf(String t) {
        int b = t.indexOf('[');
        if (b < 0) return baseType(t);
        DataType base = baseType(t.substring(0, b));
        int n = Integer.parseInt(t.substring(b + 1, t.length() - 1));
        return new ArrayDataType(base, n, base.getLength());
    }

    private String str(JsonObject o, String k) {
        return o.has(k) && !o.get(k).isJsonNull() ? o.get(k).getAsString() : null;
    }

    private void rename(Address a, String name) throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        Function f = getFunctionAt(a);
        if (f != null) {
            f.setName(name, SourceType.USER_DEFINED);
            return;
        }
        Symbol p = st.getPrimarySymbol(a);
        if (p != null && p.getSource() != SourceType.DEFAULT && !p.getName().startsWith("DAT_") && !p.getName().startsWith("LAB_")
                && !p.getName().startsWith("s_") && !p.getName().startsWith("PTR_") && !p.getName().startsWith("UNK_")) {
            if (!p.getName().equals(name)) p.setName(name, SourceType.USER_DEFINED);
            return;
        }
        if (p != null && p.getSymbolType() == SymbolType.LABEL) {
            p.setName(name, SourceType.USER_DEFINED);
            return;
        }
        Symbol s = st.createLabel(a, name, SourceType.USER_DEFINED);
        s.setPrimary();
    }

    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        JsonObject batch = JsonParser.parseString(Files.readString(Path.of(path))).getAsJsonObject();
        int tx = currentProgram.startTransaction("hp2 annotations " + Path.of(path).getFileName());
        boolean ok = false;
        try {
            apply(batch);
            ok = true;
        } finally {
            currentProgram.endTransaction(tx, ok);
        }
    }

    private void apply(JsonObject batch) throws Exception {
        Listing listing = currentProgram.getListing();
        int errors = 0;

        if (batch.has("delete_functions")) {
            for (JsonElement e : batch.getAsJsonArray("delete_functions")) {
                Address a = addr(e.getAsString());
                if (getFunctionAt(a) != null) removeFunctionAt(a);
            }
        }

        // {"start": "...", "end": "..."}: clear the listing (auto-created pointer data blocks
        // disassembly) in [start, end) and disassemble from start.
        if (batch.has("disassemble_ranges")) {
            for (JsonElement e : batch.getAsJsonArray("disassemble_ranges")) {
                JsonObject o = e.getAsJsonObject();
                Address start = addr(str(o, "start"));
                Address end = addr(str(o, "end")).subtract(1);
                listing.clearCodeUnits(start, end, false);
                if (!disassemble(start) || getInstructionAt(start) == null) {
                    println("ERROR disassemble " + start);
                    errors++;
                }
            }
        }

        if (batch.has("create_functions")) {
            for (JsonElement e : batch.getAsJsonArray("create_functions")) {
                Address a = addr(e.getAsString());
                if (getFunctionAt(a) != null) continue;
                if (getInstructionAt(a) == null) disassemble(a);
                CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                if (!cmd.applyTo(currentProgram, monitor)) {
                    println("ERROR create_function " + a + ": " + cmd.getStatusMsg());
                    errors++;
                }
            }
        }

        if (batch.has("functions")) {
            for (JsonElement e : batch.getAsJsonArray("functions")) {
                JsonObject o = e.getAsJsonObject();
                Address a = addr(str(o, "addr"));
                Function f = getFunctionAt(a);
                if (f == null) {
                    println("ERROR no function at " + a);
                    errors++;
                    continue;
                }
                try {
                    if (str(o, "name") != null) f.setName(str(o, "name"), SourceType.USER_DEFINED);
                    if (str(o, "plate") != null) f.setComment(str(o, "plate"));
                    if (str(o, "signature") != null) {
                        FunctionSignatureParser parser = new FunctionSignatureParser(currentProgram.getDataTypeManager(), null);
                        FunctionDefinitionDataType sig = parser.parse(f.getSignature(), str(o, "signature"));
                        ApplyFunctionSignatureCmd cmd = new ApplyFunctionSignatureCmd(a, sig, SourceType.USER_DEFINED);
                        if (!cmd.applyTo(currentProgram, monitor)) {
                            println("ERROR signature " + a + ": " + cmd.getStatusMsg());
                            errors++;
                        }
                    }
                    if (o.has("noreturn")) f.setNoReturn(o.get("noreturn").getAsBoolean());
                } catch (Exception ex) {
                    println("ERROR function " + a + ": " + ex);
                    errors++;
                }
            }
        }

        if (batch.has("labels")) {
            for (JsonElement e : batch.getAsJsonArray("labels")) {
                JsonObject o = e.getAsJsonObject();
                Address a = addr(str(o, "addr"));
                try {
                    if (str(o, "type") != null) {
                        DataType dt = typeOf(str(o, "type"));
                        DataUtilities.createData(currentProgram, a, dt, -1, DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
                    }
                    if (str(o, "name") != null) rename(a, str(o, "name"));
                    if (str(o, "plate") != null) listing.setComment(a, CodeUnit.PLATE_COMMENT, str(o, "plate"));
                    if (str(o, "eol") != null) listing.setComment(a, CodeUnit.EOL_COMMENT, str(o, "eol"));
                } catch (Exception ex) {
                    println("ERROR label " + a + ": " + ex);
                    errors++;
                }
            }
        }

        if (batch.has("comments")) {
            for (JsonElement e : batch.getAsJsonArray("comments")) {
                JsonObject o = e.getAsJsonObject();
                Address a = addr(str(o, "addr"));
                if (str(o, "eol") != null) listing.setComment(a, CodeUnit.EOL_COMMENT, str(o, "eol"));
                if (str(o, "pre") != null) listing.setComment(a, CodeUnit.PRE_COMMENT, str(o, "pre"));
                if (str(o, "plate") != null) listing.setComment(a, CodeUnit.PLATE_COMMENT, str(o, "plate"));
            }
        }

        // Read-back.
        int checked = 0, mismatched = 0;
        for (String key : new String[] {"functions", "labels"}) {
            if (!batch.has(key)) continue;
            for (JsonElement e : batch.getAsJsonArray(key)) {
                JsonObject o = e.getAsJsonObject();
                if (str(o, "name") == null) continue;
                Address a = addr(str(o, "addr"));
                Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
                checked++;
                if (s == null || !s.getName().equals(str(o, "name"))) {
                    println("MISMATCH " + a + " want " + str(o, "name") + " got " + (s == null ? "null" : s.getName()));
                    mismatched++;
                }
            }
        }
        println("applied: errors=" + errors + " names checked=" + checked + " mismatched=" + mismatched);
    }
}
