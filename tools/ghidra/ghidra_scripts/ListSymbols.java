// Lists symbols whose name contains one of the arguments.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class ListSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] terms = getScriptArgs();
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        for (Symbol symbol : symbols) {
            String name = symbol.getName(true);
            for (String term : terms) {
                if (name.contains(term)) {
                    println(symbol.getAddress() + " " + symbol.getSymbolType() + " " + name);
                    break;
                }
            }
        }
    }
}
