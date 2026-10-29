#!/usr/bin/env bash
# Prepare the Wine oracle for the pristine bo2.exe: install tree, prefix,
# registry values and (optionally) saves. Safe to run again.
#
# Usage: scripts/oracle/wine_setup.sh [capture_dir]
#   capture_dir  a capture with a saves/ folder to seed into the prefix
#
# Environment: WINEPREFIX overrides the prefix (default ~/.local/share/bo2-wine).
# See docs/ORACLE_WINE.md for why each step is needed.
set -euo pipefail
shopt -s globstar nullglob

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INSTALLED="$ROOT/external/installed"
OVERLAY="$ROOT/external/oracle_install"
REFERENCE_SHA256=91758398a17de9c9f974a8a102762a534fb523d40fa6868a5e5a4f0483b2753c

export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/bo2-wine}"
export WINEARCH=win32
export WINEDEBUG="${WINEDEBUG:--all}"
# No Mono/Gecko prompts on a new prefix; builtin wined3d for d3d8.
export WINEDLLOVERRIDES="mscoree,mshtml=;d3d8=b"
# Setup needs no window; the game runs under Xvfb in wine_capture.py.
unset DISPLAY WAYLAND_DISPLAY

die() { echo "wine_setup: $*" >&2; exit 1; }

for tool in wine wineboot wineserver winepath sha256sum; do
  command -v "$tool" >/dev/null || die "missing tool: $tool (see docs/SETUP.md)"
done

# 1. Pristine executable. Anything else is not the reference build.
[ -f "$INSTALLED/bo2.exe" ] || die "missing $INSTALLED/bo2.exe"
[ -f "$INSTALLED/binkw32.dll" ] || die "missing $INSTALLED/binkw32.dll"
actual="$(sha256sum "$INSTALLED/bo2.exe" | cut -d' ' -f1)"
[ "$actual" = "$REFERENCE_SHA256" ] \
  || die "bo2.exe hash $actual differs from the reference $REFERENCE_SHA256"

# 2. Install tree: the game wants the flat layout of the Windows install.
#    bo2.exe and binkw32.dll at the root, every data file by basename in data/.
mkdir -p "$OVERLAY/data"
ln -sfn "$INSTALLED/bo2.exe" "$OVERLAY/bo2.exe"
ln -sfn "$INSTALLED/binkw32.dll" "$OVERLAY/binkw32.dll"
declare -A seen=()
linked=0
for file in "$INSTALLED"/data/* "$INSTALLED"/media/gogames/bo2/**/*; do
  [ -f "$file" ] || continue
  name="${file##*/}"
  [ -z "${seen[$name]:-}" ] || die "duplicate basename: $name"
  seen[$name]=1
  ln -sfn "$file" "$OVERLAY/data/$name"
  linked=$((linked + 1))
done
[ -f "$OVERLAY/data/game.erg" ] || die "game.erg not found under $INSTALLED"
echo "install tree: $OVERLAY ($linked data files)"

# 3. Prefix.
if [ ! -f "$WINEPREFIX/system.reg" ]; then
  echo "creating prefix: $WINEPREFIX"
  mkdir -p "$WINEPREFIX"
  wineboot -u >/dev/null || die "wineboot failed (is wine32:i386 installed?)"
  wineserver -w
fi
[[ "$(head -n 5 "$WINEPREFIX/system.reg")" == *"#arch=win32"* ]] \
  || die "$WINEPREFIX is not a win32 prefix; use a new WINEPREFIX"
ln -sfn "$OVERLAY" "$WINEPREFIX/drive_c/BO2Oracle"
echo "prefix: $WINEPREFIX"

# 4. Registry. FUN_00578b80 reads the base directory from HKLM; the video
#    values select a valid mode and match wined3d's device GUID.
reg_file="$(mktemp --suffix=.reg)"
trap 'rm -f "$reg_file"' EXIT
cat >"$reg_file" <<'REG'
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\Software\Crystal Dynamics\Blood Omen 2]
"InstallPath"="C:\\BO2Oracle"
"MoviePath"="C:\\BO2Oracle\\data"

[HKEY_CURRENT_USER\Software\Crystal Dynamics\Blood Omen 2\1.00.000]
"Windowed"=dword:00000000
"ScreenWidth"=dword:00000320
"ScreenHeight"=dword:00000258
"ScreenDepth"=dword:00000016
"ModeID"=dword:00000002
"RefreshRate"=dword:00000000
"SkipMovies"=dword:00000001
"ProgramsWarnings"=dword:00000000
"GraphicsDevice"=hex:d4,cd,b2,ae,41,6e,ea,43,94,1c,83,61,cc,76,07,81
REG
wine reg import "$(winepath -w "$reg_file")" >/dev/null || die "registry import failed"
wineserver -w
echo "registry: imported"

# 5. Optional saves, from a capture directory.
if [ $# -ge 1 ]; then
  [ -d "$1/saves" ] || die "no saves/ folder in $1"
  saves="$WINEPREFIX/drive_c/users/$(id -un)/Documents/Blood Omen 2"
  mkdir -p "$saves"
  cp -a "$1/saves/." "$saves/"
  echo "saves: seeded from $1/saves"
fi
echo "done"
