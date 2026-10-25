// Disassembles and creates a function at each address.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class MakeFunction extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            Address address = toAddr(argument);
            disassemble(address);
            Function function = createFunction(address, null);
            if (function == null) {
                function = getFunctionAt(address);
            }
            println("function at " + address + ": " + (function != null ? function.getName() : "failed"));
        }
    }
}
