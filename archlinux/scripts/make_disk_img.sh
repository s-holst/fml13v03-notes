#!/usr/bin/env bash
set -euo pipefail

: "${BOOT_EXT4:=boot.ext4}"
: "${ROOT_EXT4:=root.ext4}"

WORK_DIR="$(pwd)/tmp"
DISK_IMG="${WORK_DIR}/disk.img"

die() { echo "ERROR: $*" >&2; exit 1; }
[[ -f "${BOOT_EXT4}" ]] || die "'${BOOT_EXT4}' not found"
[[ -f "${ROOT_EXT4}" ]] || die "'${ROOT_EXT4}' not found"
need_cmd() { command -v "$1" &>/dev/null || die "'$1' not found"; }
need_cmd parted
need_cmd losetup

mkdir -p "${WORK_DIR}"

# Round each image size up to the nearest MiB
BOOT_BYTES=$(stat -c%s "${BOOT_EXT4}")
ROOT_BYTES=$(stat -c%s "${ROOT_EXT4}")
BOOT_MiB=$(( (BOOT_BYTES + 1048575) / 1048576 ))
ROOT_MiB=$(( (ROOT_BYTES + 1048575) / 1048576 ))
# 1 MiB for GPT header + 1 MiB for GPT backup at the end
DISK_MiB=$(( 1 + BOOT_MiB + ROOT_MiB + 1 ))

echo "[*] Creating ${DISK_IMG} (${DISK_MiB} MiB: boot=${BOOT_MiB} MiB, root=${ROOT_MiB} MiB)"
truncate -s "${DISK_MiB}M" "${DISK_IMG}"

# Partition layout: 1 MiB gap → boot → root
BOOT_START=1
BOOT_END=$(( BOOT_START + BOOT_MiB ))
ROOT_END=$(( BOOT_END  + ROOT_MiB ))
parted -s "${DISK_IMG}" \
    mklabel gpt \
    mkpart boot ext4 "${BOOT_START}MiB" "${BOOT_END}MiB" \
    mkpart root ext4 "${BOOT_END}MiB"   "${ROOT_END}MiB"

LOOP=$(sudo losetup --find --show --partscan "${DISK_IMG}")

finalize() {
    echo "[*] Detaching loop device ${LOOP}"
    sudo losetup -d "${LOOP}"
    if [[ -f "${WORK_DIR}/success" ]]; then
        mv -v "${DISK_IMG}" .
    fi
}
trap finalize EXIT

echo "[*] Writing boot filesystem → ${LOOP}p1"
sudo dd if="${BOOT_EXT4}" of="${LOOP}p1" bs=4M status=progress conv=fsync

echo "[*] Writing root filesystem → ${LOOP}p2"
sudo dd if="${ROOT_EXT4}" of="${LOOP}p2" bs=4M status=progress conv=fsync

echo ""
echo "[*] Partition table:"
parted -s "${DISK_IMG}" print

echo ""
echo "[*] Done"

echo "1" >${WORK_DIR}/success