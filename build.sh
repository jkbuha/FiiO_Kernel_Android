#!/usr/bin/env bash
# FiiO X5iii kernel build helper — reconstructs jkbuha's original recipe.
# Usage:
#   ./build.sh config      # apply rk3188_X5_defconfig
#   ./build.sh            # build kernel.img (default)
#   ./build.sh zImage      # build zImage
#   ./build.sh clean       # make clean
#   ./build.sh mrproper    # make mrproper (full clean, drops .config)
set -e

export ARCH=arm
export CROSS_COMPILE=${CROSS_COMPILE:-$HOME/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabi/bin/arm-linux-gnueabi-}

# GCC 4.9.4 cc1 links libmpfr.so.4 (mpfr 3.x); modern distros ship .so.6.
# A compatible copy lives here so the old toolchain runs on a new host.
export LD_LIBRARY_PATH=$HOME/toolchains/extralib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# Reproducible build identity (matches original notes)
export KBUILD_BUILD_VERSION=${KBUILD_BUILD_VERSION:-33}
export KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-jkbuha}
export KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-prispewnic}

JOBS=$(nproc)
DEFCONFIG=rk3188_X5_defconfig

cmd=${1:-kernel.img}
case "$cmd" in
  config)   make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE $DEFCONFIG ;;
  clean)    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE clean ;;
  mrproper) make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper ;;
  *)        make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE "$cmd" -j"$JOBS" ;;
esac
