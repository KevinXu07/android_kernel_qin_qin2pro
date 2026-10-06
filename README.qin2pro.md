# Qin 2 Pro (s9863a1h10) kernel — WIP

Linux 4.14.199 arm64 for the DuoQin Qin 2 Pro (Unisoc SC9863A / SharkL3),
ported from Motorola's `kernel-sprd` release branch `R-11-release-RONS`.

Base: https://github.com/MotorolaMobilityLLC/kernel-sprd
Branch: `qin2pro-bringup`

## Status: WORK IN PROGRESS — boots, not daily-usable

Working:
- Boots to LineageOS 19.1 (SAR, root=/dev/mmcblk0p30)
- RM67191 576x1440 DSI panel via SPRD DPU/DSI (DRM/KMS)
- drm_hwcomposer + PowerVR Rogue GE8322 (DDK 1.10.5187610,
  `rgx_sprd` platform glue, firmware `rgx.fw.22.86.104.218`
  embedded via CONFIG_EXTRA_FIRMWARE)
- ADB over configfs (musb-hdrc.0.auto), permissive SELinux
- ramoops/pstore, framebuffer mark codes, qin_wd boot watchdog
  (panic/timeout -> recovery BCB)

Not working / WIP:
- Touchscreen (adaptive_ts firmware probe fails)
- Audio (saudio/sblock channels not ready)
- TEE / keymint / gatekeeper (Trusty not up)
- Cameras, modem/RIL, sensors hub
- Boot animation is still slow; vsync plumbing is functional but
  HWC->SF delivery needs verification

## Qin2pro-specific changes vs upstream

- `arch/arm64/kernel/qin2pro_embedded_dtb.S`: embeds merged DTB
  (`qin2pro_merged.dtb`) — the stock uboot merges dtb/dtbo at boot;
  this kernel ignores x0 and uses the embedded blob instead.
- `init/main.c`: `qin_wd` watchdog thread spawned at `kernel_init`;
  heartbeat on framebuffer at fb_base+0x54000, `/dev/qin_boot_ok`
  cancels, timeout/panic writes misc BCB -> recovery.
- `setup.c`/`fdt.c`/`mm/init.c`: framebuffer diagnostic stripes
  (`mark3()`/`fb_num()` codes — see AGENTS notes).
- `drivers/gpu/drm/sprd/`: fbdev init deferred to after DSI panel
  attach (sprd_dsi.c), alpha property standardized to DRM 0..0xffff
  semantics (sprd_dpu.c/sprd_dpu1.c), `layer->xfbc` forced off —
  IMG FBCDC modifiers must not be decoded as SPRD XFBC,
  `drm_crtc_vblank_on()` wired into the atomic enable path so
  drmWaitVBlank/vsync actually fires.
- `arch/arm64/boot/dts/sprd/ums9230.dts` + merged board DTS:
  Qin panel, reserved-memory, GPU OPPs (256/384/550 MHz,
  `sprd,dvfs-default` currently index 2 = 550 MHz).

## Build

```bash
export PATH=<los>/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin:$PATH
make O=out ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
     KCFLAGS="-Wno-error" -j$(nproc) Image
```

NOTE: `qin2pro_embedded_dtb.S` uses `.incbin`; make does not track it —
after regenerating the DTB delete `out/arch/arm64/kernel/qin2pro_embedded_dtb.o`
to force a relink.

Boot image must be header_version=1 with AVB hash footer (key held
privately); partition sizes: boot 0x2300000, recovery 0x2800000.
