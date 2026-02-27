Notes on the DC ROMA AI PC (FML13V03)
=====================================

Port layout and capabilities (top view)
```
               ======================
USB 3.0   P4->| esc              del |<-P1  USB 3.0
 Power (PD)   |                      |       Power (PD)
              |                      |       Display (DP)
USB 3.0   P3->| ctrl           < ^ > |<-P2  USB 3.0
 SWD st-link  |                      |       UART 115200
 to BMC        ----------------------
```

- Debug access:
  - SWD to board management controller (STM32) on Port 3
  - UART on Port 2

For u-boot prompt: Spam `enter` at power-on either on machine or via UART.

The python tools in this repo use [uv](https://docs.astral.sh/uv/) which is also available on RISC-V.

Contents
========

1. [Firmware and Bootchain](firmware/README.md)
2. [ArchLinux Bootstrapping](archlinux/README.md)
3. [Summary of Vendor Changes in Kernel](CustomKernel.md)

Links
=====

- https://github.com/ganboing/EIC770x-Docs
