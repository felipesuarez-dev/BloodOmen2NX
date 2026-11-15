# tools/ghidra

Headless Ghidra helpers for `bo2.exe` and `install2.dat`. The projects
live in `external/ghidra` (gitignored) and are analysed as
`x86:LE:32:default`: project `bo2` with program `bo2.exe`, project
`installer` with program `install2.dat`.

## Available helpers

| Script | Purpose |
|---|---|
| `ExportFunctions.java` | Export functions (address, name, size, thunk) to CSV |
| `ListStrings.java` | Export defined strings and their code references to TSV |
| `DecompileAt.java` | Decompile the function at or containing each address |
| `XrefsTo.java` | List references to each address |
| `ExportCalls.java` | Export every reference to a function entry (callers and data words such as vtable slots) to TSV |
| `MakeFunction.java` | Disassemble and create a function missed by auto-analysis |
| `RecoverFunctions.java` | Recover all executable targets referenced by instructions |
| `RecoverFunctionsFiltered.java` | Recover call/jump targets only |

## Usage

```sh
export JAVA_HOME=~/tools/jdk-21
GHIDRA=~/tools/ghidra_12.1.4_PUBLIC

# Decompile installer functions (read-only, already analysed)
"$GHIDRA/support/analyzeHeadless" external/ghidra installer \
  -process install2.dat \
  -scriptPath tools/ghidra/ghidra_scripts \
  -postScript DecompileAt.java 00405110 00405300 00405490 \
  -readOnly -noanalysis

# Recover a function the analyser missed, then decompile it
"$GHIDRA/support/analyzeHeadless" external/ghidra installer \
  -process install2.dat \
  -scriptPath tools/ghidra/ghidra_scripts \
  -postScript MakeFunction.java 004050a0 -noanalysis

# Export strings (writes $BO2_EXPORT_DIR/strings.tsv)
BO2_EXPORT_DIR=external/ghidra/exports_installer \
  "$GHIDRA/support/analyzeHeadless" external/ghidra installer \
  -process install2.dat \
  -scriptPath tools/ghidra/ghidra_scripts \
  -postScript ListStrings.java -readOnly -noanalysis

# Export the references to every function (writes the file given as argument)
"$GHIDRA/support/analyzeHeadless" external/ghidra bo2 -process bo2.exe \
  -scriptPath tools/ghidra/ghidra_scripts \
  -postScript ExportCalls.java re/decomp/bo2_calls/calls.tsv -readOnly -noanalysis
```

`analyzeHeadless` prefixes script output with `INFO  <Script>.java> `;
strip that prefix when saving the output.

`ExportCalls.java` writes one row per reference: `to`, `to_name`, `from`,
`from_function` (empty when the reference is a data word) and `type`. On
`bo2.exe` it takes about ten seconds and writes 132,543 references to 45,760
functions: 95,553 direct calls and 31,880 data words. With the table, "who
calls this" and "what does this call" are a text search instead of a Ghidra
run. It knows direct calls and data words only: a call through a vtable shows
as the slot that holds the address, not as its caller, and a function Ghidra
did not create is not in it. The table is a local file under `re/decomp/`.

## PyGhidra (optional)

```sh
export GHIDRA_INSTALL_DIR=~/tools/ghidra_12.1.4_PUBLIC
python3 -m venv ~/.venvs/bo2
~/.venvs/bo2/bin/pip install --no-index \
  --find-links "$GHIDRA_INSTALL_DIR/Ghidra/Features/PyGhidra/pypkg/dist" pyghidra
```

## Policy

Decompiled output is evidence, not source. Save useful output under
`re/decomp/` (gitignored). Never paste decompiled functions, original
strings or game text into the repository.

## Status

Available. The installer decompilation of M1 used `DecompileAt.java`,
`XrefsTo.java` and `MakeFunction.java`; the outputs stayed in `re/decomp/`.
The tool it produced was removed from the repository on 2026-11-15.
