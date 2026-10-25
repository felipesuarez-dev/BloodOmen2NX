# Environment setup (Ubuntu 24.04)

One-time setup for the host build and the reversing tools.

## Host build

```sh
sudo apt update
sudo apt install -y cmake ninja-build g++ clang-format p7zip-full \
  python3 python3-venv
```

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

The original game under Wine is optional and time-boxed. Wine and Xvfb are
available on the development machine; a prefix is created only when the
oracle work starts. Do not block M0 or M1 on it.

## Verify

```sh
./scripts/ci.sh
```

It runs the asset/policy check, configures the host build, compiles and
runs the tests.
