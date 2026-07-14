#!/usr/bin/env bash
# Build the 3 X5iii kernel variants and wrap each into a standalone Android
# boot.img (kernel + stock FiiO ramdisk + recomputed RK boot-id) for
# prispewnic's AROMA "MULTI" installer, which flashes boot.img straight to
# the boot partition (kernels/<freq>/boot.img).
#
#   kernels/1416/boot.img   <- 1416 MHz build   (sustained-safe)
#   kernels/1704/boot.img   <- 1704 MHz build   (balanced)
#   kernels/2016/boot.img   <- 2016 MHz build   (max, per-unit)
#   kernels/1416s/boot.img  <- stock-restore    (baseline #51 image, untouched)
#
# Each variant differs only in RK3188_T_LIMIT_FREQ. Build number is #350.
set -e
KROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPUFREQ="$KROOT/arch/arm/mach-rk3188/cpufreq.c"
FW="$KROOT/flash-work"
TEMPLATE="$FW/boot_stock.img"          # ANDROID! image: correct ramdisk/headers/pagesize
FIXID="$FW/fix_bootimg_id.py"
TREE="${1:?usage: mkbootimgs.sh <path-to-extracted-MULTI-tree>}"
ORIG_CAP=1752

export KBUILD_BUILD_VERSION=350
export KBUILD_BUILD_USER=jkbuha
export KBUILD_BUILD_HOST=prispewnic

set_cap() { sed -i "s/#define RK3188_T_LIMIT_FREQ\t([0-9]* \* 1000)/#define RK3188_T_LIMIT_FREQ\t($1 * 1000)/" "$CPUFREQ"; }
trap 'set_cap $ORIG_CAP' EXIT

test -f "$TEMPLATE" || { echo "missing template $TEMPLATE"; exit 1; }
test -d "$TREE/kernels" || { echo "no kernels/ dir under $TREE"; exit 1; }

cd "$KROOT"
for V in 1416 1704 2016; do
  echo "===================== BUILDING $V MHz (#350) ====================="
  set_cap "$V"
  grep -n 'define RK3188_T_LIMIT_FREQ' "$CPUFREQ"
  rm -f include/generated/compile.h            # force fresh #350 UTS stamp
  ./build.sh
  test -f arch/arm/boot/Image
  OUT="$TREE/kernels/$V/boot.img"
  mkdir -p "$(dirname "$OUT")"
  cp -f "$TEMPLATE" "$OUT"
  abootimg -u "$OUT" -k arch/arm/boot/Image    # swap kernel, keep stock ramdisk/headers
  python3 "$FIXID" "$OUT"                       # recompute RK SHA1 boot-id
  echo "  packaged: $OUT ($(du -h "$OUT" | cut -f1))"
done
set_cap "$ORIG_CAP"
trap - EXIT

# 1416s = "Restore Stock Kernel" payload: the untouched baseline image (valid id already).
cp -f "$TEMPLATE" "$TREE/kernels/1416s/boot.img"
echo "  stock-restore: $TREE/kernels/1416s/boot.img ($(du -h "$TREE/kernels/1416s/boot.img" | cut -f1))"

echo "ALL DONE"
grep -n 'define RK3188_T_LIMIT_FREQ' "$CPUFREQ"
