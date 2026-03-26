# Linux 6.6.92 fml13v03 Changes Summary

A LLM-generated summary of the vendor changes made in the fml13v03-6.6.92 branch of [fml13v03_linux](https://github.com/DC-DeepComputing/fml13v03_linux.git).

## Target Platform

The modified tree targets the **Eswin EIC7700/EIC7702 SoC family** ("WIN2030"), specifically the **DeepComputing FML13V03** laptop. This is a dual-die RISC-V SoC using SiFive P550-compatible cores. The vendor started from a 6.6.18 internal branch and merged upstream 6.6.92 changes on top, so the raw diff is large (~8,500 files differ), but the actual vendor additions are roughly **106 new files/directories**.

---

## New Drivers (Self-Contained, Portable)

These are additions that live in their own directories and could be ported forward with relatively little effort:

### Essential Platform Infrastructure

| Driver | Location | Notes |
|---|---|---|
| Clock controller | `drivers/clk/eswin/` | Full EIC7700 CCU, with DT bindings & `dt-bindings/clock/` headers |
| Reset controller | `drivers/reset/reset-eswin.c` | Standard reset framework |
| Pinctrl | `drivers/pinctrl/pinctrl-eic7x.c` | Pin multiplexing for EIC7x |
| Mailbox | `drivers/mailbox/eswin-mailbox.c` | Inter-processor comms (LPCPU ↔ MCPU) |
| Power domain | `drivers/pmdomain/eswin/eic770x-pmu.c` | Generic PM domain framework |
| Interconnect / NOC | `drivers/interconnect/eswin/` | 8 source files; includes profiler and sideband manager |
| IOMMU | `drivers/iommu/eswin/` | SMMU support |

### Peripherals

| Driver | Location | Notes |
|---|---|---|
| eMMC / SDHCI | `drivers/mmc/host/sdhci-eswin*.c` | Three files covering eMMC, OF variant, and SDIO/WiFi |
| PCIe (DWC) | `drivers/pci/controller/dwc/pcie-eswin.c` | DesignWare PCIe glue |
| USB DWC3 | `drivers/usb/dwc3/dwc3-eswin.c` | DWC3 glue layer |
| USB Type-C | `drivers/usb/typec/fusb303b.c` | FUSB303B TCPC controller |
| GMAC Ethernet | `drivers/net/ethernet/stmicro/stmmac/dwmac-win2030.c` | DWMAC glue |
| AHCI SATA | `drivers/ata/ahci_eswin.c` | Standard libahci glue |
| Boot SPI | `drivers/spi/spi-eswin-bootspi.c` | Boot flash controller |
| RTC | `drivers/rtc/rtc-eswin.c` | |
| PWM | `drivers/pwm/pwm-dwc-eswin.c` | For fan/backlight |
| Clocksource timer | `drivers/clocksource/timer-eswin.c` | |
| EDAC | `drivers/edac/eswin_edac.c` | DDR ECC |
| PVT sensors | `drivers/hwmon/eswin_pvt.c` | Temperature/voltage sensors |
| Fan control | `drivers/hwmon/eswin-fan-control.c` | PWM-based fan |
| Regulators | `drivers/regulator/mpq8785.c`, `tps549d22-regulator.c`, `es5340.c` | NPU and board voltage rails |
| Crypto | `drivers/crypto/eswin/` | Security engine |
| CSI-2 D-PHY | `drivers/phy/eswin/` | For camera interface |
| IMX327 sensor | `drivers/media/i2c/imx327.c` | Sony IMX327 MIPI camera sensor |
| CrOS EC power-off | `drivers/platform/chrome/cros_ec_poweroff.c` | EC-controlled power-off |
| CrOS SBS battery | `drivers/power/supply/cros-sbs-battery.c` | Smart Battery System via EC |
| PAC1934 power monitor | `drivers/hwmon/pac193x.c` | |

### Device Tree

The entire `arch/riscv/boot/dts/eswin/` directory is new — covering both EIC7700 (single-die) and EIC7702 (dual-die) board variants, including `eic7702-deepcomputing-fml13v03.dts` and OPP tables. New defconfigs: `fml13v03_defconfig`, `eic7700_defconfig`, `eic7702_defconfig`, `win2030_defconfig`.

---

## Large Proprietary Subsystems (Significant Porting Effort)

These are large, complex subsystems that are self-contained but require substantial work to keep current:

### AI Accelerators — `drivers/soc/eswin/ai_driver/`

- **NPU** (`npu/`): NVDLAv1-compatible neural processing unit with custom scheduler. ~30 source files covering the DLA engine, per-layer operators (conv, SDP, PDP, EDMA, rubik), an embedded E31 RISC-V management core, and SPRAM management.
- **DSP** (`dsp/`): Xtensa DSP subsystem with a custom ELF firmware loader (`mloader/`), DSP pool management, and SRAM allocation.
- **LPCPU** (`eswin-lpcpu.c`): Low-power CPU driver with GPIO/USB power-down sequencing.
- **Support**: `pm_eic770x.c` (SoC power management), `otp_eic770x.c` (OTP fuses), `bus_error.c`, `d2d.c` (die-to-die link), `eswin-khandle.c`.

### Display Pipeline — `drivers/gpu/drm/eswin/`

Full DRM driver: display controller (`es_dc.c`, `es_dc_hw.c`), DW-HDMI with HDCP 1.4/2.x, MIPI DSI, CRTC/plane/GEM objects, framebuffer device.

### GPU — `drivers/gpu/drm/img/img-volcanic/`

PowerVR Volcanic GPU kernel mode driver, migrated from kernel 5.17 API to 6.6 (updated `dma_resv`, `register_shrinker`, `iosys_map`, `vm_flags`).

### Media — `drivers/media/platform/eswin/` and `drivers/staging/media/eswin/`

- ISP pipeline: DVP-to-AXI bridge, ISP, MIPI CSI-2 receiver, VI top
- Video codec: H.264/H.265 decoder (`vdec/`), encoder (`venc/`), dewarp engine, hardware accelerator engine

### Memory Framework — `drivers/memory/eswin/`

Custom memory management layer: `es_buddy/`, `es_dev_buf/`, `es_mmz_vb/` (Media Memory Zone), DMA-buf heap, IOMMU reserved memory, DMA memcpy engine, procfs stats. New UAPIs under `include/uapi/linux/`.

---

## Modifications to Existing Kernel Code

These changes touch upstream files and will need to be re-applied (or upstreamed) on each kernel update:

### `arch/riscv/`

- **`mm/dma-noncoherent.c`**: Exports `arch_sync_dma_for_device/cpu`, adds `arch_teardown_dma_ops()`, hooks SMMU DMA ops into `riscv_noncoherent_register_device()`, and adds uncached DMA-buf helpers. Required by the IMG GPU driver.
- **`include/asm/dma-noncoherent.h`**: Adds `mem_type` enum for coherent vs. non-coherent region classification.
- **`kernel/module.c`**: Optimizes `count_max_entries()` from O(n²) to O(n) — a genuine upstream candidate, fixing a reported 99.63% CPU regression when loading large modules like `amdgpu.ko`.
- **`Kconfig`, `Kconfig.socs`, `Makefile`**: Registers `ARCH_ESWIN_EIC770X_SOC_FAMILY`.
- **`errata/sifive/errata.c`**: Fix to `sifive_ccache_flush64_range()`.

### `sound/soc/codecs/es8326.c`

Substantial additions to the upstream ES8326 codec driver: speaker GPIO mute control, crosstalk mixer controls, improved headphone plug/unplug sequencing to fix recording regression, and PSRR improvements.

### `drivers/gpu/drm/ttm/`

Cached TTM mappings disallowed — required for correctness of the IMG GPU on non-coherent RISC-V.

### `net/bluetooth/`

Re-introduces `a2mp.c/h` and `amp.c/h` files removed from upstream Linux, re-enabling Bluetooth AMP/A2MP support.

### Various upstream drivers

Minor modifications to `power/cros-sbs-battery.c` (PM_SLEEP fix), `drivers/hwmon/cros_ec_hwmon.c` (16-bit temperature reading), and GRUB DTB path fix in packaging scripts.

---

## Portability Summary

| Category | Effort to Port Forward |
|---|---|
| DTS + defconfigs | Low — pure data files, minimal API coupling |
| Platform infrastructure (clk, reset, pinctrl, mailbox, pmdomain) | Low — stable kernel APIs |
| Peripheral drivers (eMMC, PCIe, USB, Ethernet, SATA, RTC, etc.) | Low–Medium — standard subsystem APIs |
| `arch/riscv` patches (module.c, dma-noncoherent.c) | Medium — small but need re-review per kernel version; `module.c` change is upstreamable |
| ES8326 codec changes | Medium — upstream diff grows each release |
| Display (DRM/Eswin) | Medium–High — DRM API evolves frequently |
| IMG Volcanic GPU KMD | High — already required one major API migration |
| NPU / DSP drivers | High — large, complex, tightly coupled to firmware blobs |
| Media / ISP / VPU | High — V4L2 API evolves, staging drivers lack review |
| Custom memory framework | High — most invasive, touches UAPI, potential upstream conflict |


# Ubuntu-AI Linux Kernel `vmlinuz-6.6.92-eic7x-2025.07`

`/proc/kallsyms` don't match with the fml13v03-6.6.92 branch of [fml13v03_linux](https://github.com/DC-DeepComputing/fml13v03_linux.git).
It may include some code from [eswin's linux-stable](https://github.com/eswincomputing/linux-stable).
These trees have some shared history. Forensic analysis:

## Shared history (pre-June 2025)

All EIC7X release tags through EIC7X-2025.05 are byte-for-byte identical in both repos. This is the collaboration period.

|      Tag       | Commit date |                                 Note                                 |
|----------------|-------------|----------------------------------------------------------------------|
| EIC7X-2024.07  | 2024-08-12  | First EIC7X public release; PR merge from linmineswinncomputing fork |
| EIC702-2024.10 | 2024-10-29  | EIC702 variant release                                               |
| EIC7X-2024.08  | 2024-09-10  | Another linmineswinncomputing PR merge                               |
| EIC7X-2024.10  | 2024-11-05  | Code format compliance                                               |
| EIC7X-2024.11  | 2024-12-02  | Release 1130                                                         |
| EIC7X-2024.12  | 2025-01-26  | Critical temp fix                                                    |
| EIC7X-2025.01  | 2025-02-12  | SiFive ccache fix                                                    |
| EIC7X-2025.02  | 2025-02-28  | Memory print fix                                                     |
| EIC7X-2025.03  | 2025-04-17  | Another linmineswinncomputing PR merge                               |
| EIC7X-2025.05  | 2025-06-10  | ← Last common ancestor (divergence point)                            |


The recurring PR merges from linmineswinncomputing/linux-stable:linux-6.6.18-EIC7X (at tags EIC7X-2024.08, 2025.03, 2025.05) are the visible collaboration mechanism — an
Eswin developer's personal fork contributing back to the main eswin branch.

FML13V03-specific work begins (Mar–Jun 2025, pre-divergence)

While still tracking eswin's mainline, DeepComputing was developing FML13V03 customizations on the fml13v03-6.6.18 branch:
- FML13V03 board porting (DTS, defconfig)
- GRUB support
- ES8326 audio, touchpad fixes, Logitech backlight, Intel AX210 Wi-Fi
- Fan control, PWM, light sensors
- Some of this work flowed back to eswin (Eswin's branch has one post-divergence commit: "fix:compile img gpu builtin for fml13")

## Divergence (June 2025)

After EIC7X-2025.05, the trees split:

linux-stable / linux-6.6.18-EIC7X kept going:
- 4038 commits to HEAD (Jan 2026)
- Content: mix of upstream Linux 6.6.x stable backports (net, arm64, scsi, btrfs, crypto, etc.) + EIC7X vendor fixes
- New EIC7X tags: 2025.06 (Jul), 2025.07 (Aug), 2025.10 (Oct), 2025.12 (Jan 2026)
- Spawned a new linux-6.6.18-EIC7X-PM power management branch (S3 sleep, runtime PM) — 142 commits from the divergence point
- Notable additions: amdgpu PM support, VPU 8GB DDR, SMMU hardening, VI camera, PMIC es501x, M2 thermal, WoL

fml13v03_linux / fml13v03-6.6.92 took its own path:
- Branch formally created Aug 19, 2025 ("branch: create 6.6.92")
- 142 first-parent commits to HEAD
- Pulled eswin's PM branch twice (Oct 27 and Nov 3, 2025) via merges
- FML13V03-specific tuning, not seen in linux-stable:
- Thermal zone 55/65/80°C for FML13V03
- GPU firmware
- CONFIG_ARCH_SUSPEND_POSSIBLE for S3
- CPU frequency stability at 1.4/1.8 GHz (avoids D2D protocol errors)
- HDMI clock whitelist removal (broader monitor support)
- EEPROM serial number node
- HID/Logitech delayed_work_cb NULL fix
- Video modules set to =m to reduce boot time
- CONFIG_FUSE_FS, CONFIG_CROS_EC_CHARDEV in defconfig

EIC7X-2025.12 tag discrepancy

The EIC7X-2025.12 tag points to different commits in the two repos:
- fml13v03_linux: 52a1d7dd1578 ("fix:fix the npu oops issue")
- linux-stable: da228da68a38 ("fix(es_buddy): es buddy crash when size is unaligned")

52a1d7dd1578 is the commit immediately before da228da68a38 in linux-stable's history. Eswin placed the EIC7X-2025.12 tag, then added one more fix and moved the mutable tag
forward. DeepComputing fetched the tag before it was moved, so they have a stale snapshot.

## Summary: What's unique to each tree

Unique to linux-stable (≈4000 commits post-divergence):
- Continues incorporating upstream stable 6.6.x patches (the main bulk of commits)
- Ongoing EIC7X driver hardening across all subsystems
- Broader board support: EBC-P01, D314, D560, Vela SBC
- PMIC es501x, amdgpu PM, VPU memory scaling, ISP camera improvements
- PM branch fully integrated

Unique to fml13v03_linux (≈140 first-parent commits + PM branch merge):
- FML13V03 laptop-specific hardware tuning (thermal, power, display, audio)
- S3 suspend via the merged PM branch
- Rebased to 6.6.92 in name/organization, though not a true rebase on v6.6.92
- CPU frequency/voltage stability fixes specific to the FML13V03 use case
- Minor upstream mainline fixes (HID/Logitech) that didn't go through eswin

## Merge Effort Assessment

Special commits:
- `1badd7ce` "branch: create 6.6.92", 8366 files: A bulk sync commit — represents the delta between linux-6.6.18 and "what fml13v03 considers 6.6.92". Linux-stable has since evolved past it. Skip entirely.
- `976cbabcc` "S3 patch", 408 files: A squashed dump of fml13v03's S3 sleep work (DTS-heavy). Linux-stable has its own PM branch from the same origin. Needs cross-check.

Unique in fml13v03:
- 16 WIN2030 bug fixes (all confirmed absent from linux-stable):
  - NPU suspend/frame schedule improvements (WIN2030-19417, WIN2030-18008)
  - SMMU/DSP interaction fix (WIN2030-20740)
  - rtcwake CPU crash (WIN2030-19677)
  - Map memory failure (WIN2030-19767)
  - Display/gamma fixes (WIN2030-20026, WIN2030-20579, WIN2030-18873)
  - D2D devfreq and HAE OPP table (WIN2030-18987)
  - CPU voltage/temperature combined register (WIN2030-19163)
  - LPCPU CPU read interface (WIN2030-18074)
  - Die1 IRQ force-enable on suspend/resume (WIN2030-17874)
- ~50 FML13V03 product-specific commits: DTS tuning (thermal zones, GPIO, WiFi/BT rfkill, EEPROM SN, bootspi), defconfig changes (S3, GPU firmware, video modules).
- ~20 feature additions: CrOS EC (battery, hwmon, poweroff), OTP driver, HDMI clock whitelist removal, ES8326 PSRR improvements, camera overlays, dual SPI, light sensors.
- ~10 minor upstream-style fixes: HID/Logitech NULL deref, compile warnings, DTS warnings.

Effort estimate:
- Phase 1 — FML13V03 DTS/defconfig (Easy, ~2–3 days)
  The 50 product-specific DTS/defconfig commits. Linux-stable has the same DTS files but older. These are largely additive changes to a FML13V03-specific node tree.
  Manageable with careful diff review.
- Phase 2 — 16 WIN2030 bug fixes + feature additions (Medium, ~1–2 weeks)
  Per-commit: check if linux-stable's independent evolution already fixed the same bug; if not, cherry-pick and adapt to the evolved codebase. The NPU/DSP/HAE suspend work
  is the riskiest here — linux-stable has made its own suspend changes in those 7 months and the code may look quite different.
- Phase 3 — S3 patch reconciliation (Hard, ~1 week)
  Need to diff the 408-file "S3 patch" commit against linux-stable's current HEAD and linux-stable's PM branch to identify what's new. Much of it will already be present.
  The FML13V03-specific DTS S3 nodes are the genuine addition.

Flow:
- Cherry-pick the 137 targeted commits one-by-one (or in small logical groups) onto linux-stable's HEAD
- For each conflict in the 71 C/H files: inspect whether linux-stable already fixed it; if so, drop the commit, if not, adapt the patch to the new context
- The "branch: create 6.6.92" commit is useless for merging — throw it away
- For the "S3 patch": extract only the eic7702-deepcomputing-fml13v03.dts hunk and any S3 pieces not already in linux-stable's PM branch

The actual unique delta is smaller than it looks. The FML13V03-specific DTS changes (~15 logical changes), the 16 WIN2030 fixes, and the CrOS EC feature set are the real substance — probably 40–60 cherry-pickable patches total once the noise is stripped.