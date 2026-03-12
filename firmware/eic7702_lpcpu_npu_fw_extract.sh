#!/bin/sh

# --- Extract sub images ---
dd if=eic7702_lpcpu_npu_fw_die0.bin skip=$((0x5C)) count=5788 bs=1 of=eic7702_lpcpu_npu_fw_die0_005C.elf
dd if=eic7702_lpcpu_npu_fw_die0.bin skip=$((0x16F8)) count=27632 bs=1 of=eic7702_lpcpu_npu_fw_die0_16F8.elf
dd if=eic7702_lpcpu_npu_fw_die0.bin skip=$((0x82E8)) count=18000 bs=1 of=eic7702_lpcpu_npu_fw_die0_82E8.elf
dd if=eic7702_lpcpu_npu_fw_die1.bin skip=$((0x5C)) count=5788 bs=1 of=eic7702_lpcpu_npu_fw_die1_005C.elf
dd if=eic7702_lpcpu_npu_fw_die1.bin skip=$((0x16F8)) count=27632 bs=1 of=eic7702_lpcpu_npu_fw_die1_16F8.elf
dd if=eic7702_lpcpu_npu_fw_die1.bin skip=$((0x82E8)) count=18000 bs=1 of=eic7702_lpcpu_npu_fw_die1_82E8.elf

