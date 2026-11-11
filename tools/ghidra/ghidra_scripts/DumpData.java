// Dumps consecutive 32-bit words at an address; code targets are resolved to
// their function name. Used to walk vtables.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpData extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address address = toAddr(getScriptArgs()[0]);
        int count = Integer.decode(getScriptArgs()[1]);
        for (int index = 0; index < count; ++index) {
            Address at = address.add(index * 4L);
            long value = getInt(at) & 0xFFFFFFFFL;
            Address target = toAddr(value);
            Function function = getFunctionAt(target);
            String name = function == null ? "" : " " + function.getName();
            println(at + " = 0x" + Long.toHexString(value) + name);
        }
    }
}
