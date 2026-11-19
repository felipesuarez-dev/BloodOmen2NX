#!/usr/bin/env bash
# Verify the unpack/repack round trip of every `.big` container of the game.
#
#   BO2NX_ASSETS_DIR=$PWD/external/installed ./scripts/verify_data.sh
#   ./scripts/verify_data.sh "$BO2NX_ASSETS_DIR/media/gogames/bo2"
#
# It prints one `FAIL <path>` line per container whose repack is not
# byte-identical and the final `verified <n> of <total>`; it exits with an
# error when any container fails.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ $# -gt 1 ]]; then
    echo "usage: BO2NX_ASSETS_DIR=<dir> $0 [directory]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    directory="$1"
elif [[ -n "${BO2NX_ASSETS_DIR:-}" ]]; then
    directory="$BO2NX_ASSETS_DIR/media/gogames/bo2"
else
    echo "usage: BO2NX_ASSETS_DIR=<dir> $0 [directory]" >&2
    exit 2
fi
if [[ ! -d "$directory" ]]; then
    echo "verify_data: not a directory: $directory" >&2
    exit 1
fi

total=0
verified=0
failed=0
while IFS= read -r -d '' path; do
    total=$((total + 1))
    if output=$(python3 tools/bo2fmt/bo2fmt.py verify "$path" 2>&1) &&
       [[ "$output" == *"verified byte-identical"* ]]; then
        verified=$((verified + 1))
        continue
    fi
    failed=$((failed + 1))
    echo "FAIL $path"
done < <(find "$directory" -type f -name '*.big' -print0 | sort -z)

echo "verified $verified of $total"
if [[ "$failed" -ne 0 || "$verified" -ne "$total" ]]; then
    exit 1
fi
