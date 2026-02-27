#!/usr/bin/env bash
set -euo pipefail

: "${ROOT_SIZE:?ROOT_SIZE must be set}"
: "${ROOT_TAR:?ROOT_TAR must be /path/to/root.tar.zst}"
: "${BOOT_UUID:?BOOT_UUID must be set}"
: "${ROOT_UUID:?ROOT_UUID must be set}"

WORK_DIR="$(pwd)/tmp"
ROOT_EXT4="${WORK_DIR}/root.ext4"
MOUNT_DIR="${WORK_DIR}/mnt"

die() { echo "ERROR: $*" >&2; exit 1; }
need_cmd() { command -v "$1" &>/dev/null || die "'$1' not found"; }
need_cmd mkfs.ext4
need_cmd losetup
need_cmd mount
need_cmd zstd

finalize() {
    if mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
        echo "[*] Unmounting ${MOUNT_DIR}"
        sudo umount "${MOUNT_DIR}"
    fi
    # Detach any loop devices backed by our image
    if [[ -f "${ROOT_EXT4}" ]]; then
        local loop
        loop=$(losetup -j "${ROOT_EXT4}" | awk -F: '{print $1}')
        if [[ -n "${loop}" ]]; then
            echo "[*] Detaching loop device ${loop}"
            sudo losetup -d "${loop}"
        fi
        if [[ -f "${WORK_DIR}/success" ]]; then
            mv -v "${ROOT_EXT4}" .
        fi
    fi
}
trap finalize EXIT

echo "[*] Preparing work directory: ${WORK_DIR}"
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}" "${MOUNT_DIR}"

echo "[*] Creating ${ROOT_SIZE} image: ${ROOT_EXT4}"
truncate -s "${ROOT_SIZE}" "${ROOT_EXT4}"
mkfs.ext4 -F -O ^metadata_csum "${ROOT_EXT4}"
echo "[*] Set ${ROOT_EXT4} UUID=${ROOT_UUID}"
tune2fs -U ${ROOT_UUID} ${ROOT_EXT4}

echo "[*] Mounting image on ${MOUNT_DIR}"
LOOP_DEV=$(sudo losetup --find --show "${ROOT_EXT4}")
echo "[*] Loop device: ${LOOP_DEV}"
sudo mount "${LOOP_DEV}" "${MOUNT_DIR}"

echo "[*] Extracting ${ROOT_TAR}"
sudo tar -C "${MOUNT_DIR}" -xf "${ROOT_TAR}"

echo "[*] Copying modules"
sudo cp -a modules/lib/modules "${MOUNT_DIR}/lib"
sudo chown -R root:root "${MOUNT_DIR}/lib/modules"

echo "[*] Copying firmware"
sudo cp -a firmware "${MOUNT_DIR}/lib"
sudo chown -R root:root "${MOUNT_DIR}/lib/firmware"

echo "[*] Configuring timezone, fstab, hostname, nameserver, disable ipv6, backlisted modules, locale.gen"
sudo chroot "${MOUNT_DIR}" sh -c "ln -sf /usr/share/zoneinfo/Asia/Tokyo /etc/localtime"
sudo sh -c "echo Asia/Tokyo >$MOUNT_DIR/etc/timezone"
sudo chroot "${MOUNT_DIR}" /bin/sh << EOF
echo 'UUID=${ROOT_UUID} /       auto    defaults    1 1' >> /etc/fstab
echo 'UUID=${BOOT_UUID} /boot   auto    defaults    0 0' >> /etc/fstab
exit
EOF
sudo chroot "${MOUNT_DIR}" /bin/sh << EOF
echo arch > /etc/hostname
echo "127.0.1.1 arch" >> /etc/hosts
echo "nameserver 8.8.8.8 >> /etc/resolv.conf
echo "net.ipv6.conf.all.disable_ipv6=1" > /etc/sysctl.d/90-disable-ipv6.conf
echo "net.ipv6.conf.default.disable_ipv6=1" >> /etc/sysctl.d/90-disable-ipv6.conf
echo "blacklist evbug" >>/etc/modprobe.d/blacklist.conf
sed -i "s/#en_US.UTF-8/en_US.UTF-8/" /etc/locale.gen
exit
EOF

echo ""
df -h "${MOUNT_DIR}"
echo ""
echo "[*] Output"
cp ${ROOT_EXT4} .

#mkdir -p "${MOUNT_DIR}/lib/firmware/eic7x"
#cp -vf firmware/* "${MOUNT_DIR}/lib/firmware/eic7x"

#cp -vf rules/* "$MOUNT_DIR"/etc/udev/rules.d/ 
#sed -i '/SUBSYSTEMS=="platform", ENV{SOUND_FORM_FACTOR}="internal".*/d' "$MOUNT_DIR"/usr/lib/udev/rules.d/78-sound-card.rules

echo "1" >${WORK_DIR}/success