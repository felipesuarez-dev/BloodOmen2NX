#!/usr/bin/env bash
# Local CI: policy check, build and tests.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

./scripts/check_no_assets.sh
python3 -m unittest discover -s tests/python -v
python3 tools/gen/progress.py --check
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
