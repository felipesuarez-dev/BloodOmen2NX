// Finds instructions whose scalar operand matches one of the arguments and
// prints the containing function. Used to locate structure fields that are
// only reached through a moved base pointer.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import java.util.HashSet;
import java.util.Set;

public class FindScalarRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        Set<Long> targets = new HashSet<>();
        for (String argument : getScriptArgs()) {
            targets.add(Long.decode(argument));
        }
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            for (int index = 0; index < instruction.getNumOperands(); ++index) {
                Object[] objects = instruction.getOpObjects(index);
                for (Object object : objects) {
                    if (!(object instanceof Scalar)) {
                        continue;
                    }
                    long value = ((Scalar) object).getUnsignedValue();
                    if (!targets.contains(value)) {
                        continue;
                    }
                    Function function = getFunctionContaining(instruction.getAddress());
                    String name = function == null ? "?" : function.getEntryPoint().toString();
                    println(name + " " + instruction.getAddress() + " " + instruction);
                }
            }
        }
    }
}
