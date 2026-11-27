#!/bin/bash
# newlib for ARM, big- and little-endian, built the same way, for tools/be_check.py (a few minutes):
#   tools/be_check/newlib.sh     (into ARMEB, ~/Documents/Assets/toolchains/armeb by default; newlib 4.5 from SRC)
set -eo pipefail
unset CPATH LIBRARY_PATH
source ~/.numplay/env.sh
SRC=${SRC:-$HOME/Documents/Assets/toolchains/ndless-src/ndless-sdk/toolchain/download/newlib-4.5.0.20241231}
ARMEB=${ARMEB:-$HOME/Documents/Assets/toolchains/armeb}
for e in big little; do
  B=$ARMEB/build-$e
  PREFIX=$ARMEB/newlib-$e
  rm -rf $B $PREFIX; mkdir -p $B && cd $B
  export CFLAGS_FOR_TARGET="-m$e-endian -marm -mcpu=cortex-a15 -mfpu=vfpv4 -mfloat-abi=hard -O2 -mno-unaligned-access -ffunction-sections -fdata-sections"
  $SRC/configure --target=arm-none-eabi --prefix=$PREFIX --disable-newlib-supplied-syscalls --disable-multilib \
    --enable-newlib-nano-formatted-io --disable-newlib-io-float --disable-nls > configure.log 2>&1
  make -j5 > make.log 2>&1 && make install > install.log 2>&1 && echo "DONE $e" &
done
wait
