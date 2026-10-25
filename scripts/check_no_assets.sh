#!/usr/bin/env bash
# Fail if a game asset, original binary or local-only artifact is tracked.
# Game files live in external/ (gitignored; only external/README.txt is
# tracked). Synthetic test fixtures under tests/fixtures/ are allowed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PATTERN='\.(iso|cue|bin|chd|exe|dll|dat|erg|rkv|big|bgx|bik|ogg|wav|zip)$|^(external/|\.local/|re/decomp/|re/ghidra/)'
LEAKS="$(git ls-files \
  | grep -v '^external/README\.txt$' \
  | grep -v '^tests/fixtures/' \
  | grep -Ei "$PATTERN" || true)"
if [ -n "$LEAKS" ]; then
  echo "asset leak detected in git index:"
  echo "$LEAKS"
  exit 1
fi

FORBIDDEN="$(git ls-files \
  | grep -Ei '(^|/)(AGENTS|CLAUDE)\.md$|(^|/)\.mcp\.json$|^docs/(sessions|decomp)/' || true)"
if [ -n "$FORBIDDEN" ]; then
  echo "local-only file tracked in git index:"
  echo "$FORBIDDEN"
  exit 1
fi

echo "asset and policy check: clean"
