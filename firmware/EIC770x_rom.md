SCPU Boot ROM Analysis (WIP)
============================

**Attention: Used Ghidra + Claude - Some information below might be BS.**

This is the **masked ROM executed by the SCPU** (a SiFive E31 RV32 core) on the EIC7702 SoC. Its sole job is to load, authenticate, and launch the next stage of the boot chain. It is the hardware root of trust — it cannot be modified, and it runs before any software-controlled memory is trusted.

## Capabilities

**Multi-source boot**
Supports four boot device sources, selected by SYSCRG `+0x33c` bits [1:0]:
- `0` — SPI NOR flash (XIP from `0x5C00_0000`)
- `1` — eMMC (SDHCI controller)
- `2` — USB (mass storage gadget, DRD controller)
- `3` — UART download (serial transfer to SCPU)

**Cryptographic engine support**
- **SPAcc**: hardware AES and SM4 decryption of the bootchain image
- **PKA**: RSA and ECDSA signature verification
- **TRNG + DRBG**: seeds a NIST-compliant deterministic RNG used during crypto operations
- All three are Synopsys DesignWare IP blocks

**OTP-based key management**
Reads the following from OTP at boot:
- Encryption IV and boot image key
- RSA public key, ECDSA/ECC public key
- M2M key, HDCP key, debug password, SCPU firmware key
- Security enable bits (which algorithms are active)
- Die variant, die number, die ordinal, program lock status

**Hardware memory protection**
Configures 10 PMP (Physical Memory Protection) units in two groups (`0x21B8_0000`–`0x21BA_0000`, `0x21BB_0000`–`0x21BD_0000`), enforcing access control on the SCPU's memory space before launching untrusted code.

**Dual-die support**
Detects at runtime whether the SoC is the die-1 or die-2 of a dual-die package. Builds all MMIO pointers accordingly (`0x5...` or `0x7...`).

**CodaCache SPRAM**
Reconfigures the NPU LLC (`0x51C0_0000`) from cache mode into SPRAM (scratch-pad RAM) mode, providing ~4 MB of fast working memory at `0x5900_0000` to hold the loaded bootchain.


SCPU Memory Map
---------------

- Addresses are in the SCPU physical address space.
- SCPU security peripherals (0x21Bxxxxx) appear at MCPU address space 0x51Bxxxxx.
- Peripheral table entries marked [table] are from FUN_ram_58016e9e and may only be accessed on specific boot paths.

| Base Address | Size | Peripheral / Region | Further Reading  |
|--------------|------|---------------------|------------------|
| `0x0200_0000`  | `0x10000` | **CLINT** — SiFive Core Local Interruptor (MSIP @+0, MTIMECMP @+0x4000, MTIME @+0xBFF8) | `e31_core_complex_manual_21G1.pdf`; `run_system_scpu.py`                                 |
  | `0x0C00_0000`  | —         | **PLIC** — Platform Level Interrupt Controller (1 external source: UART0)           | `e31_core_complex_manual_21G1.pdf`; `run_system_scpu.py`  |
  | `0x2190_0000`  | —         | **SPAcc** — Synopsys Security Protocol Accelerator (SCPU view; MCPU @ `0x5190_0000`) | Synopsys DesignWare SPAcc databook; TRM Part 1; `spacc_config_init()` in ROM; boot step 6 |
  | `0x21B0_0000`  | —         | **PKA** — Synopsys Public Key Accelerator (SCPU view; MCPU @ `0x51B0_0000`)        | Synopsys DesignWare PKA databook; `FUN_ram_580178aa`; boot step 7|
  | `0x21B0_4000`  | `0x4000`  | **PKA Instruction SRAM** (SCPU view; MCPU @ `0x51B0_4000`)                         | Synopsys DesignWare PKA databook; `DAT_ram_58001d14` loaded here |
  | `0x21B0_8000`  | —         | **TRNG** — Synopsys True Random Number Generator (SCPU view; MCPU @ `0x51B0_8000`) | Synopsys DesignWare TRNG databook; `FUN_ram_58017ecc`; boot step 8  |
  | `0x21B4_0000`  | —         | **OTP Controller** (SCPU view) — `+0x38` die number/ordinal, `+0x3c` security bits | TRM Part 1; `FUN_ram_58016e22`|
  | `0x21B4_8000`  | ~`0x190`  | **OTP Shadow / Secure Identity registers** — ESWIN vendor ID written at `+0x170`   | TRM Part 1 (image-only section); `FUN_ram_5800c05a`; boot step 4 |
  | `0x21B8_0000`  | `0x8000`  | **PMP unit 0** — Physical Memory Protection (SCPU view; 10 units, stride `0x8000`) | TRM Part 1; `FUN_ram_58007ed4`; boot step 5|
  | `0x21B8_8000`  | `0x8000`  | **PMP unit 1**                                                                      | ↑ |
  | `0x21B9_0000`  | `0x8000`  | **PMP unit 2**                                                                      | ↑ |
  | `0x21B9_8000`  | `0x8000`  | **PMP unit 3**                                                                      | ↑ |
  | `0x21BA_0000`  | `0x8000`  | **PMP unit 4**                                                                      | ↑ |
  | `0x21BB_0000`  | `0x8000`  | **PMP unit 5** (group 2 starts here; gap at `0x21BAx`)                             | ↑ |
  | `0x21BB_8000`  | `0x8000`  | **PMP unit 6**                                                                      | ↑ |
  | `0x21BC_0000`  | `0x8000`  | **PMP unit 7**                                                                      | ↑|
  | `0x21BC_8000`  | `0x8000`  | **PMP unit 8**                                                                      | ↑ |
  | `0x21BD_0000`  | `0x8000`  | **PMP unit 9**                                                                      | ↑  |
  | `0x2850_0000`  | —         | **? (die-variant CodaCache window base)** — `0x4850_0000` for die-2             | `FUN_ram_58016e9e` (`_DAT_ram_300008bc`); purpose unconfirmed |
  | `0x3000_0000`  | ~`0x8000` | **SCPU SRAM (TIM)** — `.data`/`.bss` at `+0`, heap at `+0x5220`, GP at `+0x1058`  | `run_system_scpu.py`; aliased at `0x5850_0000` in SoC global map; `memory-map-soc.md` |
  | `0x5044_0000`  | `0x2000`  | **HSP SP CSR** — High-Speed Peripheral subsystem control *[table]*                 | `memory-map-soc.md` |
  | `0x5045_0000`  | `0x10000` | **SDHCI eMMC controller** *[table]*                                                 | `memory-map-soc.md`; eMMC boot path (`"emmc seek..."`, `"emmc read..."`) |
  | `0x5048_0000`  | `0x10000` | **usbdrd3_0** — USB 3.0 DRD *[table]*                                              | `memory-map-soc.md`; USB boot path; USB mass storage gadget in ROM  |
  | `0x5049_0000`  | `0x10000` | **usbdrd3_1** — USB 3.0 DRD *[table]*                                              | `memory-map-soc.md`  |
  | `0x5090_0000`  | `0x10000` | **UART0** — Synopsys DW APB UART, primary boot console (115200 baud, 9 regs used)  | `Dw_apb_uart_db.pdf`; `run_system_scpu.py`; 16550-compatible  |
  | `0x5091_0000`  | `0x10000` | **UART1** *[table]*                                                                 | `memory-map-soc.md`; UART download boot path (`"boot sel change to uart"`) |
  | `0x50A0_0000`  | `0x10000` | **Mailbox** — U84 (MCPU) → SCPU *[table]*                                          | `memory-map-soc.md` |
  | `0x5160_0000`  | `0x200000`| **PINCTRL** *[table]*                                                               | `memory-map-soc.md`; TRM |
  | `0x5180_0000`  | `0x8000`  | **bootspi** — SPI flash controller *[table]*                                        | `memory-map-soc.md`; SPI NOR boot path  |
  | `0x5181_0000`  | `0x8000`  | **SYSCON** — System Controller; boot progress codes at `+0x668`–`+0x674`           | `memory-map-soc.md`; `FUN_ram_58008d34` |
  | `0x5182_0000`  | —         | **? (AON domain, unidentified)** *[table]*                                          | `FUN_ram_58016e9e`; not present in `memory-map-soc.md`; likely AONCRG or PMCRG |
  | `0x5182_8000`  | `0x8000`  | **SYSCRG** — System Clock & Reset Generator (7 PLLs: spll0/1/2, vpll, apll, CPU, DDR) | TRM Part 1 §3.2; `FUN_ram_58008dbe`; `FUN_ram_580001c0`; `memory-map-soc.md` |
  | `0x5184_0000`  | `0x8000`  | **TIMER0** *[table]*                                                                | `memory-map-soc.md`; TRM  |
  | `0x5190_0000`  | —         | **? (SCPU peripheral table entry)** *[table]*                                       | `FUN_ram_58016e9e`; MCPU map shows SPAcc here — may be second instance or MCPU-side alias |
  | `0x51C0_0000`  | `0x200000`| **CodaCache LLC** — NPU LLC controller, initialised as SPRAM during boot           | TRM Part 1; `coda_llc_spram_init()`; `FUN_ram_58007ce8`; `memory-map-soc.md` |
  | `0x51D8_8000`  | `0x1000`  | **NPU LLC0 / DW AXI DMA** — accessed as `_DAT_ram_300008c0 + 0x188000`            | TRM Part 1 DMA register table (`CH1_CTL @+0x118`); trace analysis; `memory-map-soc.md` |
  | `0x51D8_9000`  | `0x1000`  | **NPU LLC1 / DW AXI DMA** — accessed as `_DAT_ram_300008c0 + 0x189000`            | ↑  |
  | `0x5210_0000`  | `0x50000` | **Die-to-Die (eic7x-d2d)** *[table]*                                               | `memory-map-soc.md` |
  | `0x5230_0000`  | `0x40000` | **DDR Controller 0** *[table]*                                                      | `memory-map-soc.md`; TRM |
  | `0x5238_0000`  | `0x40000` | **DDR Controller 1** *[table]*                                                      | `memory-map-soc.md`; TRM |
  | `0x5300_0000`  | `0x800000`| **DDR PHY 0** *[table]*                                                             | `memory-map-soc.md`  |
  | `0x5380_0000`  | `0x800000`| **DDR PHY 1** *[table]*                                                             | `memory-map-soc.md` |
  | `0x5800_0000`  | `0x20000` | **SCPU Boot ROM** — masked ROM, SCPU-only                                           | ROM binary; `run_system_scpu.py`; `memory-map-soc.md`  |
  | `0x5900_0000`  | ~`0x400000` | **CodaCache0 SRAM** — boot chain load target (`0x7900_0000` for die-2)        | `memory-map-soc.md`; `FUN_ram_58016e9e` (`DAT_ram_30000888`) |
  | `0x5C00_0000`  | `0x4000000` | **NOR Flash XIP** — SPI NOR direct-map window; bootchain read from here           | `memory-map-soc.md`; `rom_decompiled.c:1292–1296` |



0x58000000: ROM
---------------

- Entry point
- sets mtvec, stack, hart check
- C-runtime init

0x30000000: SRAM
----------------

- .data copy from ROM (0x5801a5b8 → 0x30000000, ~0x888 bytes)
- working memory / BSS


0x51d88000 and 0x51d89000: coda_llc_spram control
-------------------------------------------------

some kind of coda cache controller / cfg

```
+0x100[0] 1=busy, 0=ready
+0x118 = 0xffff: operation: Init valid entries
+0x118 = 0x10000: operation: TagArray

tag array init:
+0x28 = 0;
+0x2c = 0;
+0x20 = 0xf0000;
+0x24 = 0x3fff;
+0x20 = [+0x20] | 1
+0x10 = 3;
+0x154 = 3;
+0x18 = 4;
+0x140 = 0x23;
```


Boot Flow
---------

1. **Hardware bring-up**
   - Hart 0 takes over; hart 1+ spin-wait
   - `.bss` zeroed, `.data` copied from ROM into SCPU SRAM (`0x3000_0000`)
   - GP set to `0x3000_1058`, heap initialised at `0x3000_5220`

2. **Clock init** (`FUN_ram_58008dbe`)
   - Read SYSCRG `pll_status` (`+0xa4`) & `0x3f`
   - If all 6 PLLs locked: switch SCPU clock to PLL via SYSCRG `+0x20c`, `+0x10c`, `+0x160`
   - If PLLs not locked: remain on bypass clock
   - Init UART0 at 115200 baud; print `"pll failed."` if PLLs did not lock

3. **Die variant detection** (`FUN_ram_58016e22`)
   - Read OTP `0x21b4_003c`: security mode, die number, die ordinal
   - Rebuild entire MMIO pointer table (`FUN_ram_58016e9e`)
   - Standard die: peripheral base `0x4000_0000`, CodaCache at `0x5900_0000`
   - die-2: peripheral base `0x6000_0000`, CodaCache at `0x7900_0000`

4. **CodaCache LLC → SPRAM** (`coda_llc_spram_init`)
   - LLC0 at `0x51D8_8000`, LLC1 at `0x51D8_9000`
   - Clear valid entries, configure CCUCMWVR (`+0x118`)
   - Switch to SPRAM mode; this becomes the load target for the bootchain

5. **OTP / Secure Identity provisioning** (`FUN_ram_5800c05a`) — boot step 4
   - Write ESWIN vendor ID string to OTP shadow at `0x21B4_8000 +0x170`
   - Enable/mark OTP rows via stride-8 control registers

6. **PMP init** (`FUN_ram_58007ed4(1, 3, 1)`) — boot step 5
   - Unlock and reset all 10 PMP units across two groups
   - Configure memory protection regions and access permissions

7. **Crypto peripheral init** (`FUN_ram_58016ffe`)
   - SPAcc init at `0x2190_0000` (`spacc_config_init`) — boot step 6
   - PKA init at `0x21B0_0000`, load curve params from ROM `0x5800_1d14` — boot step 7
   - TRNG init at `0x21B0_8000`, 256-byte entropy pool at SRAM `0x3000_3140` — boot step 8

8. **Security material loaded from OTP** (`FUN_ram_58016c3c` / `FUN_ram_580086ac`)
   - Read function list, security list, device/market IDs
   - Read chip ID string into SRAM `0x3000_020c`
   - Read RSA public key into `0x3000_3034`; read ECC public key, IV, boot key
   - Verify device ID and market ID against OTP

9. **Boot device selection** (`FUN_ram_58016e56`)
   - Read SYSCRG `+0x33c` bits [1:0] into boot context `+0x4`
   - Print `"bootsel[N]"`
   - If NOR flash (bootsel = 0): send XON (`0x11`) over UART

10. **Bootchain load & verify** (`FUN_ram_58007c40`)
    - NOR flash: read from `0x5C00_0000` (SPI XIP)
    - eMMC: SDHCI driver, read by block
    - USB: USB mass storage gadget (DRD controller)
    - UART: serial download, print `"Downloading..."` / `"failed!"`
    - For all sources: decrypt with SPAcc (AES or SM4), verify signature with PKA (RSA or ECDSA)
    - Keys and IV sourced from OTP
    - On success: verified image resides in CodaCache SPRAM

11. **Launch** (`FUN_ram_58018a3a`)
    - Clean up heap, release crypto context
    - Jump to verified bootchain entry point in CodaCache SPRAM
    - On any failure: print `"bootstrap is failed: N (0xN)"` and loop forever; write boot step code `10000` to SYSCON `+0x668`


Key Design Observations
-----------------------

- **Boot progress codes** written to SYSCON `+0x668`–`+0x674` at each stage allow external hardware or MCPU to observe where the SCPU is in the boot sequence.
- The ROM contains a **complete cryptographic stack**: AES, SM4, RSA, ECDSA, TRNG/DRBG — all hardware-accelerated, no software crypto.
- All four boot sources share a common **verify-then-launch** path; the only difference is how bytes are read.
- The ROM is **MCPU-invisible** (physical address `0x5800_0000` not accessible from P550) — the SCPU boots autonomously and is expected to have launched its firmware before the MCPU is released from reset.
- **Failure is non-recoverable** by design: the ROM has no retry logic for crypto failures, only an infinite error loop, enforcing the hardware root-of-trust model.
