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
```

`analyzeHeadless` prefixes script output with `INFO  <Script>.java> `;
strip that prefix when saving the output.

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

Available. The installer decompilation that produced
`tools/bo2fmt/installer.py` used `DecompileAt.java`, `XrefsTo.java` and
`MakeFunction.java`; the outputs stayed in `re/decomp/`.
