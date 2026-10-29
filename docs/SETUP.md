# Environment setup (Ubuntu 24.04)

One-time setup for the host build and the reversing tools.

## Host build

```sh
sudo apt update
sudo apt install -y cmake ninja-build g++ clang-format p7zip-full \
  python3 python3-venv libsdl2-dev libgl1-mesa-dev xvfb
```

The SDL2 + OpenGL backend and its Xvfb smoke test need `libsdl2-dev`,
`libgl1-mesa-dev` and `xvfb`. CMake skips the backend when they are
missing; use `-DBO2NX_ENABLE_SDL=OFF` to force a headless build.

Already present on the development machine, no install needed: `git`,
`cmake` 3.28, `gcc` 13, `python3`, `uv`, `cargo`, `node`, `bsdtar`,
`ffmpeg`, ImageMagick.

## Reversing

Ghidra and a JDK 21 are required for the binary analysis. The sibling
project pins Ghidra 12.1.4, which is a known-good version for the same
era of MSVC binaries.

1. Install JDK 21. On this machine the container has no system Java, so
   the JDK is unpacked under `~/tools/jdk-21` and the analysis commands
   set `JAVA_HOME=~/tools/jdk-21`.
2. Download Ghidra from `https://ghidra-sre.org` and unzip it under
   `~/tools/ghidra`.
3. Create a local Python environment outside the repository for `pefile`
   and the Ghidra Python bindings:

```sh
python3 -m venv ~/.venvs/bo2
~/.venvs/bo2/bin/pip install pefile
```

The Ghidra project lives in `external/ghidra` and is never committed.

## Switch target (M2)

devkitPro is already installed at `/opt/devkitpro` on the development
machine, with `libnx` and the `deko3d` headers. The Switch CMake preset is
added with the platform backend; see `docs/RUNNING.md`.

## Oracle (optional)

The Wine oracle needs a 32-bit Wine, a virtual display and input and capture
tools:

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install -y wine wine32:i386 xvfb xdotool ffmpeg imagemagick
```

The prefix is created by `scripts/oracle/wine_setup.sh`; see
[`ORACLE_WINE.md`](ORACLE_WINE.md). The oracle is time-boxed: do not block M0
or M1 on it.

## Verify

```sh
./scripts/ci.sh
```

It runs the asset/policy check, configures the host build, compiles and
runs the tests.
