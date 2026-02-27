# Linux 6.6.92 fml13v03 Changes Summary

Below is an LLM-generated summary of the vendor changes made in the fml13v03-6.6.92 branch of [fml13v03_linux](https://github.com/DC-DeepComputing/fml13v03_linux.git).

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
