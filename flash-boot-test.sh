#!/usr/bin/env bash
# FiiO X5iii (RK3188) boot-test flash helper.
# Strategy: back up the current boot partition, swap in ONLY the new kernel
# (keeps the stock ramdisk / cmdline / load offsets), flash, reboot.
# The backup doubles as your recovery image. RK3188 also has maskrom recovery,
# so a bad kernel is recoverable, not a brick.
#
# Requires (AUR on CachyOS):  paru -S --needed android-tools rkflashtool abootimg
# Run with the player connected. NOTHING is written until you type YES.
set -e

KDIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$KDIR/arch/arm/boot/Image"
OUT="$KDIR/flash-work"
mkdir -p "$OUT"; cd "$OUT"

echo "== checks =="
[ -f "$IMG" ] || { echo "!! $IMG missing — build the kernel first"; exit 1; }
for t in rkflashtool abootimg; do
  command -v "$t" >/dev/null || { echo "!! missing tool: $t  (paru -S rkflashtool abootimg)"; exit 1; }
done
echo "kernel: $IMG ($(du -h "$IMG" | cut -f1))"

echo
echo "== STEP 1: put the player into loader mode =="
echo "   With USB debugging on:   adb reboot bootloader"
echo "   (or power on holding the loader key combo for your unit)"
read -r -p "Press Enter once the device is in loader mode... "
echo "-- probing with rkflashtool --"
rkflashtool n || { echo "!! rkflashtool can't see the device. Check cable/mode."; exit 1; }

echo
echo "== STEP 2: back up the current boot partition (RECOVERY IMAGE) =="
rkflashtool r boot > boot_stock.img
cp -f boot_stock.img boot_stock.SAVE.img     # pristine copy we never touch
echo "backed up: $(ls -l boot_stock.img | awk '{print $5" bytes"}')  -> $OUT/boot_stock.SAVE.img"
# sanity: Android boot images start with the magic "ANDROID!"
head -c 8 boot_stock.img | grep -q "ANDROID!" \
  && echo "boot image looks like standard Android format (good for abootimg)" \
  || echo "NOTE: no ANDROID! magic — use the mkbootimg fallback below instead of abootimg."

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
