#!/bin/sh

# --- Extract raw blobs ---
dd if=spi2_0_D2D.bin skip=$((0x1a40)) count=$((0x9640)) bs=1 of=spi2_0_D2D_serdes_text.bin
dd if=spi2_0_D2D.bin skip=$((0xb0c0)) count=$((0x1540)) bs=1 of=spi2_0_D2D_serdes_data.bin
dd if=spi2_0_D2D.bin skip=$((0xd500)) count=$((0xd300)) bs=1 of=spi2_0_D2D_npu_nim.bin
dd if=spi2_0_D2D_npu_nim.bin skip=$((92)) count=$((5880-92)) bs=1 of=spi2_0_D2D_npu_nim1.elf
dd if=spi2_0_D2D_npu_nim.bin skip=$((5880)) count=$((33512-5880)) bs=1 of=spi2_0_D2D_npu_nim2.elf
dd if=spi2_0_D2D_npu_nim.bin skip=$((33512)) bs=1 of=spi2_0_D2D_npu_nim3.elf

dd if=spi2_0_D2D.bin skip=$((0x1a840)) count=$((0x54140)) bs=1 of=spi2_0_D2D_pmix_fw.bin

dd if=spi2_0_D2D.bin skip=$((0x6e9c0)) count=$((0x35540)) bs=1 of=spi2_0_D2D_pmix_fw2.bin
dd if=spi2_0_D2D_pmix_fw2.bin skip=104 count=$((5892-104)) bs=1 of=spi2_0_D2D_pmix_fw2_1.elf
dd if=spi2_0_D2D_pmix_fw2.bin skip=5892 count=$((33524-5892)) bs=1 of=spi2_0_D2D_pmix_fw2_2.elf
dd if=spi2_0_D2D_pmix_fw2.bin skip=33524 bs=1 of=spi2_0_D2D_pmix_fw2_3.elf


# Extract and disassemble ARC EM SerDes firmware from spi2_0_D2D.bin
#
# Memory map (ARC EM local address space):
#   .text  @ 0x00080000  size 0x9640  (spi2_0_D2D.bin offset 0x1a40)
#   .data  @ 0x00090000  size 0x1540  (spi2_0_D2D.bin offset 0xb0c0)

OBJCOPY=${OBJCOPY:-arc-snps-elf-objcopy}
OBJDUMP=${OBJDUMP:-arc-snps-elf-objdump}

TEXT_VMA=0x80000
DATA_VMA=0x90000

# --- Build combined ELF with correct VMAs ---
# Step 1a: binary → ELF (.data section, VMA=0)
$OBJCOPY \
    -I binary \
    -O elf32-littlearc \
    --set-section-flags .data=alloc,load,readonly,code \
    spi2_0_D2D_serdes_text.bin \
    spi2_0_D2D_serdes_text_tmp.elf

# Step 1b: rename .data → .text
$OBJCOPY \
    --rename-section .data=.text \
    spi2_0_D2D_serdes_text_tmp.elf \
    spi2_0_D2D_serdes_text_renamed.elf

# Step 1c: now .text exists — set its VMA
$OBJCOPY \
    --change-section-vma .text=$TEXT_VMA \
    spi2_0_D2D_serdes_text_renamed.elf \
    spi2_0_D2D_serdes_text.elf

# Step 2: add .data section at its VMA into the combined ELF
$OBJCOPY \
    --add-section .data=spi2_0_D2D_serdes_data.bin \
    --set-section-flags .data=alloc,load,data \
    --change-section-vma .data=$DATA_VMA \
    spi2_0_D2D_serdes_text.elf \
    spi2_0_D2D_serdes_combined.elf

# Verify section VMAs
echo "Section headers:"
$OBJDUMP -h spi2_0_D2D_serdes_combined.elf

$OBJDUMP -d \
    -M cpu=arcem,fpuda,dsp,spfp,dpfp,quarkse_em \
    -m EM \
    -EL \
    spi2_0_D2D_serdes_combined.elf \
    > spi2_0_D2D_serdes.asm
