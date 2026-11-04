#!/usr/bin/env bash
# The fx-CG50's toolchain (port.mk's FXCG), built from its makers' sources in the folder given: GNU binutils 2.44 and
# GCC 14.2 for sh3eb-elf, newlib 4.5 (sourceware.org) for the add-ins' calling convention, GCC again with newlib for
# C++ and libstdc++, and libfxcg (github.com/Jonimoose/libfxcg) in libfxcg.a. Linux and macOS, about an hour.
#   Ubuntu: sudo apt install build-essential git curl libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev texinfo
#   macOS: brew install gmp mpfr libmpc
#   platform/casio-cg/toolchain.sh ~/toolchains/casio
set -euo pipefail
unset CPATH LIBRARY_PATH # (the cross compiler would take the computer's headers)
BINUTILS=binutils-2.44.tar.bz2 BINUTILS_SUM=f66390a661faa117d00fab2e79cf2dc9d097b42cc296bf3f8677d1e7b452dc3a
GCC=gcc-14.2.0.tar.xz GCC_SUM=a7b39bc69cbf9e25826c5a60ab26477001f7c08d85cec04bc0e29cabed6f3cc9
NEWLIB=newlib-4.5.0.20241231.tar.gz NEWLIB_SUM=33f12605e0054965996c25c1382b3e463b0af91799001f5bb8c0630f2ec8c852
LIBFXCG=daa76d9e37758ab5b9b0f614b7e0aa6acfbe2e18
DIR=${1:?usage: toolchain.sh DIR}
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
SRC=$DIR/src B=$DIR/build
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
export PATH="$DIR/sh3eb/bin:$PATH"

sha256() { if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
# fetch FILE SUM URL...: the source tarball from the first place that has it as released, unpacked in SRC
fetch() {
  local f=$1 sum=$2 url
  shift 2
  [ -d "$SRC/${f%.tar.*}" ] && return
  mkdir -p "$SRC"
  for url; do
    curl -fsSL --retry 3 -o "$SRC/$f" "$url/$f" && [ "$(sha256 "$SRC/$f")" = "$sum" ] && break
    rm -f "$SRC/$f"
  done
  [ -f "$SRC/$f" ] || { echo "toolchain.sh: no $f as released" >&2; exit 1; }
  tar -xf "$SRC/$f" -C "$SRC"
}
# built STEP VERSIONS: whether that step is done, with these versions
built() { [ "$(cat "$DIR/.built-$1" 2>/dev/null)" = "$2" ]; }

HOST=""
if [ "$(uname -s)" = Darwin ]; then
  b=$(brew --prefix)
  HOST="--with-gmp=$b/opt/gmp --with-mpfr=$b/opt/mpfr --with-mpc=$b/opt/libmpc"
  [ -d "$b/opt/isl" ] && HOST="$HOST --with-isl=$b/opt/isl"
fi
# (GCC's sources nest brackets deeper than clang allows by default)
if cc -v 2>&1 | grep clang > /dev/null; then export CXXFLAGS="-fbracket-depth=512 -O2"; fi

if ! built binutils "$BINUTILS"; then
  fetch "$BINUTILS" "$BINUTILS_SUM" https://ftpmirror.gnu.org/gnu/binutils https://mirrors.kernel.org/gnu/binutils
  rm -rf "$B/binutils" && mkdir -p "$B/binutils" && cd "$B/binutils"
  "$SRC/${BINUTILS%.tar.*}/configure" --target=sh3eb-elf --prefix="$DIR/sh3eb" --disable-nls --disable-werror \
    --with-sysroot --disable-gdb --disable-gprofng --with-system-zlib --enable-warn-rwx-segments=no
  make -j"$JOBS"
  make install
  echo "$BINUTILS" > "$DIR/.built-binutils"
fi

# GCC, C only, to build newlib with
if ! built gcc "$GCC"; then
  fetch "$GCC" "$GCC_SUM" "https://ftpmirror.gnu.org/gnu/gcc/${GCC%.tar.*}" \
    "https://mirrors.kernel.org/gnu/gcc/${GCC%.tar.*}"
  rm -rf "$B/gcc" && mkdir -p "$B/gcc" && cd "$B/gcc"
  # shellcheck disable=SC2086
  "$SRC/${GCC%.tar.*}/configure" --target=sh3eb-elf --prefix="$DIR/sh3eb" --with-pkgversion=NumPlay \
    --without-headers --enable-languages=c --disable-tls --disable-nls --disable-threads --disable-shared \
    --disable-libssp --disable-libvtv --disable-libada --disable-libquadmath --disable-libgomp \
    --with-endian=big --with-multilib-list=m4a-nofpu --with-system-zlib --disable-werror $HOST
  make -j"$JOBS" inhibit_libc=true all-gcc
  make install-gcc
  make -j"$JOBS" inhibit_libc=true all-target-libgcc
  make install-target-libgcc
  echo "$GCC" > "$DIR/.built-gcc"
fi

if ! built newlib "$NEWLIB"; then
  fetch "$NEWLIB" "$NEWLIB_SUM" https://sourceware.org/pub/newlib
  rm -rf "$B/newlib" && mkdir -p "$B/newlib" && cd "$B/newlib"
  export CFLAGS_FOR_TARGET="-mb -m4a-nofpu -mhitachi -Os -ffunction-sections -fdata-sections -DMALLOC_PROVIDED \
-DABORT_PROVIDED -DMISSING_SYSCALL_NAMES"
  "$SRC/${NEWLIB%.tar.*}/configure" --target=sh-elf CC_FOR_TARGET=sh3eb-elf-gcc AR_FOR_TARGET=sh3eb-elf-ar \
    RANLIB_FOR_TARGET=sh3eb-elf-ranlib AS_FOR_TARGET=sh3eb-elf-as LD_FOR_TARGET=sh3eb-elf-ld \
    NM_FOR_TARGET=sh3eb-elf-nm OBJCOPY_FOR_TARGET=sh3eb-elf-objcopy --prefix="$DIR/newlib" \
    --disable-newlib-supplied-syscalls --disable-multilib --enable-newlib-nano-formatted-io \
    --disable-newlib-io-float --disable-nls
  make -j"$JOBS"
  make install
  echo "$NEWLIB" > "$DIR/.built-newlib"
fi

# GCC again, C and C++, with newlib where it looks for its C library: libstdc++, for the fx-CG like newlib
if ! built gcc-cxx "$GCC $NEWLIB"; then
  fetch "$GCC" "$GCC_SUM" "https://ftpmirror.gnu.org/gnu/gcc/${GCC%.tar.*}" \
    "https://mirrors.kernel.org/gnu/gcc/${GCC%.tar.*}"
  mkdir -p "$DIR/sh3eb/sh3eb-elf/include" "$DIR/sh3eb/sh3eb-elf/lib"
  cp -R "$DIR/newlib/sh-elf/include/." "$DIR/sh3eb/sh3eb-elf/include/"
  cp "$DIR"/newlib/sh-elf/lib/lib{c,m,g}.a "$DIR/sh3eb/sh3eb-elf/lib/"
  rm -rf "$B/gcc-cxx" && mkdir -p "$B/gcc-cxx" && cd "$B/gcc-cxx"
  export CFLAGS_FOR_TARGET="-mb -m4a-nofpu -mhitachi -Os -ffunction-sections -fdata-sections"
  export CXXFLAGS_FOR_TARGET="$CFLAGS_FOR_TARGET"
  # shellcheck disable=SC2086
  "$SRC/${GCC%.tar.*}/configure" --target=sh3eb-elf --prefix="$DIR/sh3eb" --with-pkgversion=NumPlay --with-newlib \
    --enable-languages=c,c++ --disable-tls --disable-nls --disable-threads --disable-shared --disable-libssp \
    --disable-libvtv --disable-libada --disable-libquadmath --disable-libgomp --disable-libstdcxx-verbose \
    --disable-libstdcxx-pch --with-endian=big --with-multilib-list=m4a-nofpu --with-system-zlib --disable-werror \
    $HOST
  make -j"$JOBS" all-gcc
  make install-gcc
  make -j"$JOBS" all-target-libgcc
  make install-target-libgcc
  make -j"$JOBS" all-target-libstdc++-v3
  make install-target-libstdc++-v3
  echo "$GCC $NEWLIB" > "$DIR/.built-gcc-cxx"
fi

# libfxcg: its syscalls and helpers compiled one by one (its own Makefiles want a toolchain set up its way)
if ! built libfxcg "$LIBFXCG"; then
  if [ "$(git -C "$DIR/libfxcg" rev-parse HEAD 2>/dev/null)" != "$LIBFXCG" ]; then
    rm -rf "$DIR/libfxcg"
    git init -q "$DIR/libfxcg"
    git -C "$DIR/libfxcg" fetch -q --depth 1 https://github.com/Jonimoose/libfxcg.git "$LIBFXCG"
    git -C "$DIR/libfxcg" checkout -q FETCH_HEAD
  fi
  rm -rf "$B/fxcg" && mkdir -p "$B/fxcg" "$DIR/sdk/lib"
  flags=(-ffunction-sections -fdata-sections -Os -mb -m4a-nofpu -mhitachi -ffreestanding -std=gnu99
         -I"$DIR/libfxcg/include")
  for f in "$DIR"/libfxcg/libfxcg/syscalls/*.S "$DIR"/libfxcg/libfxcg/misc/*.S "$DIR"/libfxcg/libfxcg/misc/*.c; do
    sh3eb-elf-gcc "${flags[@]}" -c "$f" -o "$B/fxcg/$(basename "$f").o"
  done
  rm -f "$DIR/sdk/lib/libfxcg.a"
  sh3eb-elf-ar rcs "$DIR/sdk/lib/libfxcg.a" "$B"/fxcg/*.o
  echo "$LIBFXCG" > "$DIR/.built-libfxcg"
fi

rm -rf "$B" "$SRC"
sh3eb-elf-g++ --version | sed -n 1p
echo "The fx-CG50's toolchain is in $DIR (FXCG)"
