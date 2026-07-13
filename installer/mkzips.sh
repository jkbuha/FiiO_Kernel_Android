#!/usr/bin/env bash
# Rebuild the 3 X5iii installer zips off the current (DVFS-reverted) tree.
# Each variant differs only in RK3188_T_LIMIT_FREQ (the CPU freq cap).
# Uses python3 zipfile (host has no `zip`). Builds to a temp file then moves,
# so a failure never destroys an existing good zip.
set -e
KROOT="$(cd "$(dirname "$0")/.." && pwd)"
INST="$KROOT/installer"
CPUFREQ="$KROOT/arch/arm/mach-rk3188/cpufreq.c"
BUILD="$INST/build"
STAMP=2026.07
ORIG_CAP=1752

set_cap() { sed -i "s/#define RK3188_T_LIMIT_FREQ\t([0-9]* \* 1000)/#define RK3188_T_LIMIT_FREQ\t($1 * 1000)/" "$CPUFREQ"; }
trap 'set_cap $ORIG_CAP' EXIT

zipdir() {  # zipdir <out.zip> <srcdir> <entry...>
  local out="$1" src="$2"; shift 2
  python3 - "$out" "$src" "$@" <<'PY'
import sys, os, zipfile
out, src = sys.argv[1], sys.argv[2]
roots = sys.argv[3:]
files = []
for r in roots:
    base = os.path.join(src, r)
    for dp, _, fn in os.walk(base):
        for f in fn:
            full = os.path.join(dp, f)
            files.append((full, os.path.relpath(full, src)))
files.sort(key=lambda x: x[1])
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for full, arc in files:
        z.write(full, arc)
print("  entries:", len(files))
PY
}

cd "$KROOT"
for V in 1416 1752 2016; do
  echo "===================== BUILDING $V MHz ====================="
  set_cap "$V"
  grep -n 'define RK3188_T_LIMIT_FREQ' "$CPUFREQ"
  ./build.sh
  test -f arch/arm/boot/Image
  cp -f arch/arm/boot/Image "$BUILD/install/kernel/zImage"
  ZIP="$INST/FiiO-X5III-jkbuha-$STAMP-$V.zip"
  TMP="$ZIP.tmp"
  rm -f "$TMP"
  zipdir "$TMP" "$BUILD" META-INF install
  mv -f "$TMP" "$ZIP"
  echo "  packaged: $ZIP  ($(du -h "$ZIP" | cut -f1))"
done
set_cap "$ORIG_CAP"
trap - EXIT
echo "ALL DONE"
grep -n 'define RK3188_T_LIMIT_FREQ' "$CPUFREQ"
