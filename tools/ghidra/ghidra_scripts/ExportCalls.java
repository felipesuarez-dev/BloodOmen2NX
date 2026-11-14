// Writes every reference to a function entry as a TSV file: who calls each
// function and which data words (vtable slots, callback tables) point at it.
// Argument: the output file.
// Columns: to, to_name, from, from_function, type.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.BufferedWriter;
import java.io.FileWriter;

public class ExportCalls extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            println("usage: ExportCalls <output.tsv>");
            return;
        }
        FunctionManager functions = currentProgram.getFunctionManager();
        int functionCount = 0;
        int referenceCount = 0;
        try (BufferedWriter out = new BufferedWriter(new FileWriter(arguments[0]))) {
            out.write("to\tto_name\tfrom\tfrom_function\ttype\n");
            FunctionIterator iterator = functions.getFunctions(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Function function = iterator.next();
                Address entry = function.getEntryPoint();
                functionCount++;
                ReferenceIterator references =
                    currentProgram.getReferenceManager().getReferencesTo(entry);
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Address from = reference.getFromAddress();
                    Function caller = functions.getFunctionContaining(from);
                    out.write(entry + "\t" + function.getName() + "\t" + from + "\t"
                        + (caller == null ? "" : caller.getEntryPoint().toString()) + "\t"
                        + reference.getReferenceType() + "\n");
                    referenceCount++;
                }
            }
        }
        println("functions " + functionCount + " references " + referenceCount);
    }
}
