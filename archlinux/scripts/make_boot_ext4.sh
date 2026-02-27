#!/usr/bin/env bash
set -euo pipefail

: "${BOOT_SIZE:?BOOT_SIZE must be set}"
: "${BOOT_UUID:?BOOT_UUID must be set}"
: "${IMAGEGZ:=vmlinuz}"
: "${DTB:=dtb}"
: "${INITRD:=initrd}"

WORK_DIR="$(pwd)/tmp"
BOOT_EXT4="${WORK_DIR}/boot.ext4"
MOUNT_DIR="${WORK_DIR}/mnt"

die() { echo "ERROR: $*" >&2; exit 1; }
[[ -f "${IMAGEGZ}" ]] || die "'${IMAGEGZ}' not found"
[[ -f "${DTB}"    ]] || die "'${DTB}' not found"
[[ -f "${INITRD}" ]] || die "'${INITRD}' not found"
need_cmd() { command -v "$1" &>/dev/null || die "'$1' not found"; }
need_cmd mkfs.ext4
need_cmd losetup
need_cmd mount

finalize() {
    if mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
        echo "[*] Unmounting ${MOUNT_DIR}"
        sudo umount "${MOUNT_DIR}"
    fi
    # Detach any loop devices backed by our image
    if [[ -f "${BOOT_EXT4}" ]]; then
        local loop
        loop=$(losetup -j "${BOOT_EXT4}" | awk -F: '{print $1}')
        if [[ -n "${loop}" ]]; then
            echo "[*] Detaching loop device ${loop}"
            sudo losetup -d "${loop}"
        fi
        if [[ -f "${WORK_DIR}/success" ]]; then
            mv -v "${BOOT_EXT4}" .
        fi
    fi
}
trap finalize EXIT

echo "[*] Preparing work directory: ${WORK_DIR}"
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}" "${MOUNT_DIR}"

echo "[*] Creating ${BOOT_SIZE} image: ${BOOT_EXT4}"
truncate -s "${BOOT_SIZE}" "${BOOT_EXT4}"
mkfs.ext4 -L boot "${BOOT_EXT4}"
echo "[*] Set ${BOOT_EXT4} UUID=${BOOT_UUID}"
tune2fs -U ${BOOT_UUID} "${BOOT_EXT4}"


echo "[*] Mounting image on ${MOUNT_DIR}"
LOOP_DEV=$(sudo losetup --find --show "${BOOT_EXT4}")
echo "[*] Loop device: ${LOOP_DEV}"
sudo mount "${LOOP_DEV}" "${MOUNT_DIR}"

sudo cp -v "${IMAGEGZ}" "${MOUNT_DIR}/vmlinuz"
sudo cp -v "${DTB}" "${MOUNT_DIR}/dtb"
sudo cp -v "${INITRD}" "${MOUNT_DIR}/initrd"
sudo mkdir -p "${MOUNT_DIR}/extlinux"
sudo sh <<ENDSH
cat >"${MOUNT_DIR}/extlinux/extlinux.conf" <<EOF
## /boot/extlinux/extlinux.conf
default arch
timeout 50
label arch
    linux /vmlinuz
    initrd /initrd
    fdt /dtb
    append root=UUID=${ROOT_UUID} console=tty0 rootfstype=ext4 rootwait rw selinux=0 LANG=en_US.UTF-8 audit=0
EOF
ENDSH

sync
echo ""
echo "[*] Boot image contents:"
ls -lh "${MOUNT_DIR}"
#cat ${MOUNT_DIR}/extlinux/extlinux.conf
echo ""
df -h "${MOUNT_DIR}"
echo ""
echo "1" >${WORK_DIR}/success

# cleanup() will unmount, detach the loop device, and copy boot.ext4 to PWD