// Prints the references to the given addresses.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class XrefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            Address address = toAddr(argument);
            println("xrefs to " + address);
            ReferenceIterator references =
                currentProgram.getReferenceManager().getReferencesTo(address);
            while (references.hasNext()) {
                Reference reference = references.next();
                println("  " + reference.getFromAddress() + " " + reference.getReferenceType());
            }
        }
    }
}
