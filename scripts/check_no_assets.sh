#!/usr/bin/env bash
# Fail if a game asset, an original binary or a local-only artifact is
# tracked. Game files live in external/ (gitignored; only external/README.txt
# is tracked).
#
# Three checks over every tracked file:
#   1. names: a denied extension or a local-only directory;
#   2. content: a known game, image, audio, video, archive or executable
#      signature in the first bytes;
#   3. binaries: any file with a NUL byte in its first 8 KiB.
# A file passes checks 1 to 3 only when it is in ALLOWED below. No image of
# the game goes into the repository, not even a frame rendered by the port;
# add an entry only with the maintainer's approval.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PATTERN='\.(iso|cue|bin|chd|img|nrg|mdf|exe|dll|sys|pdb|so|dylib|o|obj|lib|a|dat|erg|rkv|big|bgx|bik|bk2|san|ogg|wav|mp3|mp4|mkv|avi|wmv|dds|tga|bmp|png|jpg|jpeg|gif|tif|tiff|ppm|pgm|psd|raw|fnt|slm|zip|7z|rar|gz|xz|bz2|tar|cab|msi|nro|nsp|xci|sav)$|^(external/|\.local/|re/decomp/|re/ghidra/)'

ALLOWED=(
  "external/README.txt"
)

is_allowed() {
  local name="$1" entry
  for entry in "${ALLOWED[@]}"; do
    [ "$name" = "$entry" ] && return 0
  done
  return 1
}

LEAKS=""
while IFS= read -r name; do
  is_allowed "$name" && continue
  if printf '%s\n' "$name" | grep -Eiq "$PATTERN"; then
    LEAKS+="$name"$'\n'
  fi
done < <(git ls-files)
if [ -n "$LEAKS" ]; then
  echo "asset leak detected in git index (denied name):"
  printf '%s' "$LEAKS"
  exit 1
fi

# Content: signatures and binary files. The allow list is passed to Python
# so the same exceptions apply.
CONTENT="$(git ls-files -z | python3 -c '
import sys

allowed = set(sys.argv[1:])
signatures = [
    (b"goefile\0", "game container (.big)"),
    (b"texfont\0", "game font entry"),
    (b"DDS ", "DDS texture"),
    (b"OggS", "Ogg audio"),
    (b"RIFF", "RIFF audio or video"),
    (b"BIK", "Bink video"),
    (b"KB2", "Bink 2 video"),
    (b"MZ", "Windows executable"),
    (b"\x7fELF", "ELF executable"),
    (b"Rar!", "RAR archive"),
    (b"PK\x03\x04", "ZIP archive"),
    (b"7z\xbc\xaf\x27\x1c", "7-Zip archive"),
    (b"\x1f\x8b", "gzip data"),
    (b"\x89PNG", "PNG image"),
    (b"\xff\xd8\xff", "JPEG image"),
    (b"GIF8", "GIF image"),
    (b"CD001", "ISO image"),
]
bad = []
for name in sys.stdin.buffer.read().split(b"\0"):
    if not name:
        continue
    path = name.decode("utf-8", "surrogateescape")
    if path in allowed:
        continue
    try:
        with open(path, "rb") as handle:
            head = handle.read(8192)
    except OSError:
        continue  # deleted in the work tree but still in the index
    kind = next((label for magic, label in signatures if head.startswith(magic)), None)
    if kind is None and b"\0" in head:
        kind = "binary file"
    if kind is not None:
        bad.append(f"{path}: {kind}")
print("\n".join(bad))
' "${ALLOWED[@]}")"
if [ -n "$CONTENT" ]; then
  echo "asset leak detected in git index (content):"
  echo "$CONTENT"
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
