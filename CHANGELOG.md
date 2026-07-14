# Changelog

FiiO X5iii (RK3188T) custom kernel — kernelmods.com

## 3.5 — 2026-07-13 (build #350)

Toolchain: Linaro GCC 4.9.4-2017.01 · Base: Linux 3.0.101+ · Android 5.1.1

### Security & hardening
- Backport **Dirty COW** (CVE-2016-5195) — `mm/memory.c`, `include/linux/mm.h` `[8564ad52]`
- **futex** requeue_pi guard (CVE-2014-3153) — `kernel/futex.c` `[d08b4bd5]`
- Enable **CC_STACKPROTECTOR** — `arch/arm/configs/rk3188_X5_defconfig` `[35674b8f]`

### Block layer correctness
- `elevator`: propagate `elevator_init_fn()` error → fixes NULL `q->elevator` oops on init-alloc failure — `block/elevator.c` `[6f3f9781]`
- `blk-throttle`: fix undefined `blkio_policy_throtl` symbol — `block/blk-throttle.c` `[d135d055]`

### CPU / DVFS / power
- Cap `RK3188_T_LIMIT_FREQ` at 1752 MHz, build-time overridable → **1416 / 1704 / 2016** clock variants — `arch/arm/mach-rk3188/cpufreq.c` `[fde6693c]`
- **Revert the 312/504 MHz DVFS floor** — the 825 mV undervolt caused a screen-off wake-hang (the CPU wedged at the low OPP during suspend) — `arch/arm/mach-rk3188/board-rk3188-sdk.c` `[e6edb5c6]`
- **Driver trim**: disable USB serial / GSM-modem and GTCO tablet drivers (lower RAM, faster boot) — `rk3188_X5_defconfig` `[29c08e21]`

### Audio — AK4490 DAC (`sound/soc/codecs/ak4490.c`)
- `selected_filter` module param now switches the digital filter **live**, via `kernel_param_ops` (input validated, applied to both DACs) `[cd3ec60f]`
  - `echo 0..4 > /sys/module/snd_soc_ak4490/parameters/selected_filter`
  - 0 sharp · 1 slow · 2 short-delay-sharp · 3 short-delay-slow · 4 super-slow
- Default filter = **slow roll-off**; `selected_filter` made authoritative — re-applied on every playback setup so the choice survives track/rate changes `[6c2e3d79]`
- Native **DSD64 / DSD128 / DSD256** confirmed working (via FiiO Music; DSD512 correctly disabled — unsupported by the AK4490)

### Compat & cleanup
- Restore `__DEFERRED_WORK_INITIALIZER` compat for the Mali module — `include/linux/workqueue.h` `[137fcd2d]`
- Remove leftover merge-conflict markers and a stale `autosmp.c.rej` (non-ARM arch files) `[f6264231]`

### Build / flash / packaging
- `build.sh` — reconstructs the original Linaro 4.9.4-2017.01 toolchain build recipe `[ad15cef1]`
- `flash-boot-test.sh` — backup/flash helper with `--backup-only` `[5a26ed03, 137fcd2d]`
- `installer/` — ROM-agnostic TWRP/AROMA installer: keeps the user's ramdisk, swaps only the kernel, and **recomputes the RK boot ID** so the bootloader accepts it (no drop to recovery). Includes `mkzips.sh` packager, the `rkbootrepack` tool, and the 1416/1704/2016 zips `[88e8b097]`
- `installer/mkbootimgs.sh` — packager for prispewnic's **AROMA "MULTI" installer**: builds the three kernel variants (#350) and wraps each into a full standalone `boot.img` (kernel + stock ramdisk + recomputed RK boot-id) for the `kernels/{1416,1704,2016}/` slots, plus the untouched baseline image in `1416s/` (the "Restore Stock Kernel" slot). Output: `FiiO-X5iii-jkbuha-V3.5-TWRP-MULTI.zip` `[238518f2]`

### Notes
Device-side changes made during development but **not part of this kernel** (apply separately if desired): governor swap to a tuned CAF `interactive` (init.d `60Main`), `audio.offload.disable=1` (`build.prop`, avoids a ViPER4Android + compress-offload AudioFlinger crash), and the Google Play Services / GApps restoration.

---

Base: **release 3.4 Beta3** (`11325a49`) and earlier.
