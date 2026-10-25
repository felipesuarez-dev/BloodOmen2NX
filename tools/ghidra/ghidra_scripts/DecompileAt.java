// Decompiles the function at or containing each address.
// @category BO2
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (String argument : getScriptArgs()) {
            Address address = toAddr(argument);
            Function function = getFunctionAt(address);
            if (function == null) {
                function = getFunctionContaining(address);
            }
            if (function == null) {
                println("no function at " + address);
                continue;
            }
            DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
            println("===== " + function.getEntryPoint() + " " + function.getName() + " =====");
            println(results.getDecompiledFunction().getC());
        }
    }
}
