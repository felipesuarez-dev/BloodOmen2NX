// Exports the function inventory of the current program to functions.csv.
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.File;
import java.io.PrintWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = System.getenv("BO2_EXPORT_DIR");
        if (outPath == null) {
            outPath = ".";
        }
        File dir = new File(outPath);
        dir.mkdirs();
        File out = new File(dir, "functions.csv");
        try (PrintWriter writer = new PrintWriter(out)) {
            writer.println("address,name,size,thunk,external");
            FunctionIterator functions =
                currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext()) {
                Function function = functions.next();
                writer.printf(
                    "%s,%s,%d,%s,%s%n",
                    function.getEntryPoint(),
                    function.getName(),
                    function.getBody().getNumAddresses(),
                    function.isThunk(),
                    function.isExternal());
            }
        }
        println("wrote " + out.getAbsolutePath());
    }
}
