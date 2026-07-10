#!/usr/bin/env bash
# FiiO X5iii (RK3188) boot-test flash helper.
#
# Modes:
#   ./flash-boot-test.sh --backup-only   Dump the current boot partition and stop.
#   ./flash-boot-test.sh                 Back up, swap in the new kernel, flash, reboot.
#
# Strategy for the full run: back up the current boot partition, swap in ONLY the
# new kernel (keeps the stock ramdisk / cmdline / load offsets), flash, reboot.
# The backup doubles as your recovery image. RK3188 also has maskrom recovery,
# so a bad kernel is recoverable, not a brick.
#
# Requires (AUR on CachyOS):  paru -S --needed android-tools rkflashtool abootimg
#   (--backup-only needs only rkflashtool.)
# Run with the player in loader mode. NOTHING is written until you type YES.
set -e

MODE=flash
for a in "$@"; do
  case "$a" in
    --backup-only) MODE=backup ;;
    -h|--help)
      echo "usage: $0 [--backup-only]"
      echo "  (no args)       back up boot, swap kernel, flash, reboot"
      echo "  --backup-only   just dump the current boot partition, then stop"
      exit 0 ;;
    *) echo "!! unknown arg: $a  (see --help)"; exit 1 ;;
  esac
done

KDIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$KDIR/arch/arm/boot/Image"
OUT="$KDIR/flash-work"
mkdir -p "$OUT"; cd "$OUT"

echo "== checks (mode: $MODE) =="
command -v rkflashtool >/dev/null || { echo "!! missing tool: rkflashtool  (paru -S rkflashtool)"; exit 1; }
if [ "$MODE" = flash ]; then
  command -v abootimg >/dev/null || { echo "!! missing tool: abootimg  (paru -S abootimg)"; exit 1; }
  [ -f "$IMG" ] || { echo "!! $IMG missing — build the kernel first (./build.sh)"; exit 1; }
  echo "new kernel: $IMG ($(du -h "$IMG" | cut -f1))"
fi

echo
echo "== STEP 1: put the player into loader mode =="
echo "   With USB debugging on:   adb reboot bootloader"
echo "   (or power on holding the loader key combo for your unit)"
read -r -p "Press Enter once the device is in loader mode... "
echo "-- probing with rkflashtool --"
rkflashtool n || { echo "!! rkflashtool can't see the device. Check cable/mode."; exit 1; }

echo
echo "== STEP 2: back up the current boot partition (RECOVERY IMAGE) =="
if rkflashtool r boot > boot_stock.img 2>/dev/null && [ -s boot_stock.img ]; then
  echo "read 'boot' partition by name."
else
  echo "!! 'rkflashtool r boot' (by name) failed — your rkflashtool build is the"
  echo "   offset-based classic. Reading the partition table so you can dump by offset:"
  rkflashtool p > parm.txt 2>/dev/null || true
  echo "   ---- partitions (size@offset, in 512-byte sectors) ----"
  grep -iE "CMDLINE|mtdparts|\(boot\)|@" parm.txt 2>/dev/null | head || cat parm.txt 2>/dev/null | head
  echo "   -------------------------------------------------------"
  echo "   Find the (boot) entry 'SIZE@OFFSET' and run, e.g.:"
  echo "       rkflashtool r 0x10000 0x8000 > $OUT/boot_stock.img"
  echo "   then re-run this script."
  exit 1
fi
cp -f boot_stock.img boot_stock.SAVE.img     # pristine copy we never touch
echo "backed up: $(ls -l boot_stock.img | awk '{print $5" bytes"}')  -> $OUT/boot_stock.SAVE.img"
# sanity: Android boot images start with the magic "ANDROID!"
if head -c 8 boot_stock.img | grep -q "ANDROID!"; then
  echo "boot image looks like standard Android format (good for abootimg)."
  ANDROID_OK=1
else
  echo "NOTE: no ANDROID! magic at offset 0 — abootimg may not handle it;"
  echo "      use the mkbootimg fallback printed at the end."
  ANDROID_OK=0
fi

if [ "$MODE" = backup ]; then
  echo
  echo "== BACKUP COMPLETE — nothing was flashed. =="
  echo "  saved: $OUT/boot_stock.SAVE.img"
  echo "  restore anytime (in loader mode):"
  echo "     rkflashtool w boot < $OUT/boot_stock.SAVE.img && rkflashtool b"
  echo "  reboot the player now without changes:"
  echo "     rkflashtool b"
  exit 0
fi

echo
echo "== STEP 3: swap in the new kernel (keeps stock ramdisk/cmdline/offsets) =="
cp -f boot_stock.img boot_new.img
abootimg -u boot_new.img -k "$IMG"
echo "built: $(ls -l boot_new.img | awk '{print $5" bytes"}')"

echo
echo "== STEP 4: FLASH (this overwrites the boot partition) =="
read -r -p "Type YES to flash boot_new.img: " ok
[ "$ok" = "YES" ] || { echo "aborted — nothing written."; exit 1; }
rkflashtool w boot < boot_new.img
echo "== STEP 5: reboot =="
rkflashtool b
echo
echo "Done. Watch the player boot."
echo "If it does NOT boot: re-enter loader mode and restore the stock kernel:"
echo "  cd $OUT && rkflashtool w boot < boot_stock.SAVE.img && rkflashtool b"
echo
echo "---- FALLBACK (if abootimg failed / no ANDROID! magic) : your proven mkbootimg recipe ----"
echo "  abootimg -x boot_stock.img            # extract -> bootimg.cfg, zImage, initrd.img"
echo "  mkbootimg --base 0x60400000 --pagesize 16384 --ramdisk_offset 0x62000000 \\"
echo "            --second_offset 0x60f00000 --tags_offset 0x60088000 \\"
echo "            --cmdline \"\$(sed -n 's/^cmdline = //p' bootimg.cfg)\" \\"
echo "            --kernel $IMG --ramdisk initrd.img -o boot_new.img"
echo "  rkflashtool w boot < boot_new.img && rkflashtool b"
