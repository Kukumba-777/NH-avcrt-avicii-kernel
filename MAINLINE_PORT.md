# Mainline port: OnePlus Nord (avicii / SM7250 "lito")

Goal chosen by owner: a real postmarketOS-style **mainline** port — a clean upstream
Linux 6.x kernel with a device tree + drivers for this phone. This is a multi-month,
hardware-specific effort; full hardware support is not guaranteed. This file tracks the
plan and what has been done so the work survives across sessions.

## Reality check
- This repo is the **downstream** Oplus/OnePlus **4.19.325** vendor kernel. It cannot be
  "upgraded" into mainline by editing it — mainline is a separate codebase. We *harvest*
  hardware addresses from here and feed them into a fresh upstream tree.
- 4.19 is EOL; 4.19.325 is already the last release of that series.
- No accessible UART on the retail Nord → first-boot debugging uses **ramoops/pstore**.

## Hardware facts (harvested from arch/arm64/boot/dts/vendor/20801/)
- RAM base: `0x80000000`
- ramoops/pstore: base `0xCBE00000`, size `0x400000` (console `0x40000`, pmsg `0x200000`)
- smem `0x80900000`, cmd_db `0x80860000`
- SoC = SM7250 (lito). Closest upstream base: `sc7180.dtsi` / sm7250 family.

## Build environment status (this machine)
- Present: clang 21, llvm-ar/objcopy (versioned), git, make, bc, bison, flex.
- **MISSING (blocker for linking the kernel Image):** a linker. Need ONE of:
  - `sudo apt install lld` (gives `ld.lld`, recommended — build with `LLVM=1`), or
  - `sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu`
- Optional: `device-tree-compiler` (kernel builds its own dtc, so not strictly needed).
- DTB-only builds (`make dtbs`) work WITHOUT the cross-linker (host-side).

## Milestones
1. [done] Harvest hardware data from downstream tree.
2. [blocked] Set up build env — clone upstream stable, install a linker (needs sudo).
3. [in progress] Write minimal `sm7250-oneplus-avicii.dts` and validate via `make dtbs`.
4. [ ] Build Image.gz + dtb, repack the device's own boot.img (AnyKernel3 / magiskboot)
       to preserve base/offsets. **Do not flash without explicit owner confirmation.**
5. [ ] Bring-up loop: pstore log → fix clocks/regulators/smmu → display (simplefb) →
       USB → storage (UFS) → then WiFi/touch/audio/modem. Each is its own sub-project.

## Flashing safety
Phone bootloader must be unlocked (confirmed `unlocked: yes` via fastboot). Repack the
original boot.img rather than guessing mkbootimg params. Always keep a known-good boot.img
backup before flashing. **Only ever `fastboot boot` (RAM) during bring-up, never flash.**

## Current state (2026-06-10)
- Work dir: `/home/niftox/avicii-mainline-port/` (do NOT use /home/niftox/avcrt — separate
  project). Kernel source: `/home/niftox/linux-mainline` (Linux 6.12).
- Created `arch/arm64/boot/dts/qcom/sm7250.dtsi` (SoC base = sc7180; GIC/gcc/RSC/cmd-db
  addresses verified identical to downstream lito) and `sm7250-oneplus-avicii.dts`
  (compatible `qcom,sm7250`), with the full fixed reserved-memory map ported 1:1 from
  downstream lito.dtsi (mpss/adsp/cdsp/npu/camera/video/wlan/ipa/gpu). Compiles clean.
- Build: `PATH=~/llvm-bin:$PATH make ARCH=arm64 LLVM=1 Image.gz qcom/sm7250-oneplus-avicii.dtb`
- Repack: `/tmp/mkbi` (AOSP mkbootimg.py + gki stub); kernel section must be Image.gz with
  the dtb APPENDED (avicii ABL convention). Test image: `boot-mainline-6.12-it4.img`.

## Two hard blockers (the real work ahead)
1. **No mainline clock/display drivers for lito.** drivers/clk/qcom has no gcc-lito /
   dispcc/gpucc/videocc-lito. gcc-sc7180 binds at the right address but programs the wrong
   tree. Porting these C drivers is required for UFS/USB/display/GPU — weeks each.
2. **No observability.** Kernel hangs before any console output; no UART exposed on retail
   Nord. it0-it4 all show a frozen logo, empty pstore. Need a USB-C SBU UART cable to
   debug the early hang, otherwise iteration is blind.

## Iteration history
it0 plain Image.gz / it1 +memory+fbcon+pstore / it2 +appended-dtb / it4 sm7250 base +
full reserved-memory map. All: frozen logo, no adb, empty pstore (early silent hang).
