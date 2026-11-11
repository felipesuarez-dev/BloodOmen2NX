// Creates the function at each address when Ghidra has no boundary there and
// decompiles it. The animation/model vtable targets in this binary were not
// picked up by auto analysis.
// @category BO2
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CreateAndDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (String argument : getScriptArgs()) {
            Address address = toAddr(argument);
            Function function = getFunctionAt(address);
            if (function == null) {
                int transaction = currentProgram.startTransaction("create function");
                try {
                    function = createFunction(address, null);
                } finally {
                    currentProgram.endTransaction(transaction, function != null);
                }
            }
            if (function == null) {
                println("cannot create function at " + address);
                continue;
            }
            DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
            println("===== " + function.getEntryPoint() + " " + function.getName() + " =====");
            println(results.getDecompiledFunction().getC());
        }
    }
}
