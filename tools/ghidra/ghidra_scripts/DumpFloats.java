// Print float values at the given addresses.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class DumpFloats extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            Address address = toAddr(argument);
            int bits = currentProgram.getMemory().getInt(address);
            println(argument + " " + Float.intBitsToFloat(bits));
        }
    }
}
