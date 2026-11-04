#!/usr/bin/env bash
# Ndless's SDK for the TI-Nspire, built from its source (github.com/ndless-nspire/Ndless, r2023 and the build fixes
# after it) in the folder given: its GCC 14 and newlib (ndless-sdk/toolchain/build_toolchain.sh, without GDB), then
# libsyscalls, libndls, the third-party libraries and the tools (genzehn, make-prg, luna). port.mk's NDLESS_SDK is
# then DIR/ndless-sdk. Linux and macOS, about an hour.
#   Ubuntu: sudo apt install build-essential git curl libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
#           libboost-program-options-dev texinfo flex bison
#   macOS: brew install gmp mpfr libmpc boost texinfo
#   platform/nspire/toolchain.sh ~/toolchains/ndless
set -euo pipefail
REV=e52415fb568e92cd36ed049067b3e9c2727b84af
DIR=${1:?usage: toolchain.sh DIR}
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
SDK=$DIR/ndless-sdk
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}

# the source at REV, with its submodules (nspire-io, zlib, freetype, luna)
if [ "$(git -C "$DIR" rev-parse HEAD 2>/dev/null)" != "$REV" ]; then
  [ -d "$DIR/.git" ] || git -C "$DIR" init -q
  git -C "$DIR" fetch -q --depth 1 https://github.com/ndless-nspire/Ndless.git "$REV"
  git -C "$DIR" checkout -q FETCH_HEAD
fi
git -C "$DIR" submodule update -q --init --depth 1 || git -C "$DIR" submodule update -q --init

# the compiler: build_toolchain.sh skips what its .built_* files name, so GDB is left out
cd "$SDK/toolchain"
sed -n 's/^GDB=\([^ ]*\).*/\1/p' build_toolchain.sh | tr -d '\n' > .built_gdb
script=build_toolchain.sh
if [ "$(uname -s)" = Darwin ]; then
  # (Homebrew's GMP, MPFR and MPC, as build_toolchain.sh says to add)
  b=$(brew --prefix)
  sed "s|^OPTIONS_GCC=\"|OPTIONS_GCC=\"--with-gmp=$b/opt/gmp --with-mpfr=$b/opt/mpfr --with-mpc=$b/opt/libmpc |" \
    build_toolchain.sh > .numplay_build_toolchain.sh
  script=.numplay_build_toolchain.sh
  export LIBRARY_PATH="$b/lib"
fi
PYTHON='' PARALLEL=-j$JOBS CFLAGS="${CFLAGS:--O2 -std=gnu17}" sh "./$script"
rm -rf download build build-gcc .numplay_build_toolchain.sh

# the SDK's libraries and tools
export PATH="$SDK/bin:$SDK/toolchain/install/bin:$PATH"
if [ "$(uname -s)" = Darwin ]; then
  # (genzehn's Makefile has no place for Homebrew's Boost: built here, make then finds it made)
  (cd "$SDK/tools/genzehn" && c++ -Wall -std=c++11 -O2 -I elfio-3.2 -I"$b/include" genzehn.cpp -L"$b/lib" \
    -lboost_program_options -lz -o ../../bin/genzehn)
fi
make -C "$SDK" -j"$JOBS" build-libsyscalls build-libndls build-thirdparty build-tools
nspire-gcc --version | sed -n 1p
for t in genzehn make-prg; do [ -x "$SDK/bin/$t" ] || { echo "toolchain.sh: no $t" >&2; exit 1; }; done
echo "Ndless's SDK is in $SDK (NDLESS_SDK)"
