// Exports defined strings with their code references (TSV).
// @category BO2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.File;
import java.io.PrintWriter;

public class ListStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = System.getenv("BO2_EXPORT_DIR");
        if (outPath == null) {
            outPath = ".";
        }
        File dir = new File(outPath);
        dir.mkdirs();
        File out = new File(dir, "strings.tsv");
        try (PrintWriter writer = new PrintWriter(out)) {
            writer.println("address\tvalue\txrefs");
            DataIterator data = currentProgram.getListing().getDefinedData(true);
            while (data.hasNext()) {
                Data item = data.next();
                Object value = item.getValue();
                if (!(value instanceof String)) {
                    continue;
                }
                String text = (String) value;
                if (text.length() < 4) {
                    continue;
                }
                StringBuilder refs = new StringBuilder();
                ReferenceIterator references =
                    currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    if (refs.length() > 0) {
                        refs.append(';');
                    }
                    refs.append(reference.getFromAddress());
                }
                String safe = text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n");
                writer.printf("%s\t%s\t%s%n", item.getAddress(), safe, refs);
            }
        }
        println("wrote " + out.getAbsolutePath());
    }
}
