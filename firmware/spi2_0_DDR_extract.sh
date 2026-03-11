#!/bin/sh

# --- Extract sub images ---
dd if=spi2_0_DDR.bin skip=$((0x9005C)) count=5788 bs=1 of=spi2_0_DDR_9005C.elf
dd if=spi2_0_DDR.bin skip=$((0x916F8)) count=27632 bs=1 of=spi2_0_DDR_916F8.elf
dd if=spi2_0_DDR.bin skip=$((0x982E8)) count=18000 bs=1 of=spi2_0_DDR_982E8.elf
dd if=spi2_0_DDR.bin skip=$((0xA005C)) count=5788 bs=1 of=spi2_0_DDR_A005C.elf
dd if=spi2_0_DDR.bin skip=$((0xA16F8)) count=27632 bs=1 of=spi2_0_DDR_A16F8.elf
dd if=spi2_0_DDR.bin skip=$((0xA82E8)) count=18000 bs=1 of=spi2_0_DDR_A82E8.elf

