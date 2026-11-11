// Lists the instructions that reference one of the given addresses (memory
// operands included, which the scalar search does not see).
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.util.HashSet;
import java.util.Set;

public class FindAddressRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        Set<Long> targets = new HashSet<>();
        for (String argument : getScriptArgs()) {
            targets.add(toAddr(argument).getOffset());
        }
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            for (Reference reference : instruction.getReferencesFrom()) {
                Address to = reference.getToAddress();
                if (to == null || !targets.contains(to.getOffset())) {
                    continue;
                }
                Function function = getFunctionContaining(instruction.getAddress());
                String name = function == null ? "?" : function.getEntryPoint().toString();
                println(name + " " + instruction.getAddress() + " " + instruction);
            }
        }
    }
}
