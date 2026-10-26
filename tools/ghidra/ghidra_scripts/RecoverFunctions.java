// Recovers executable targets referenced by instructions but not defined as functions.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;

public class RecoverFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        int candidates = 0;
        int recovered = 0;
        for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
            for (int operand = 0; operand < instruction.getNumOperands(); operand++) {
                for (Reference reference : instruction.getOperandReferences(operand)) {
                    Address address = reference.getToAddress();
                    if (address == null || getFunctionContaining(address) != null) {
                        continue;
                    }
                    MemoryBlock block = currentProgram.getMemory().getBlock(address);
                    if (block == null || !block.isExecute()) {
                        continue;
                    }
                    ++candidates;
                    if (!disassemble(address)) {
                        continue;
                    }
                    Function function = createFunction(address, null);
                    if (function != null) {
                        ++recovered;
                        println("recovered " + address);
                    }
                }
            }
        }
        println("recovery candidates=" + candidates + " functions=" + recovered);
    }
}
