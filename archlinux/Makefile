# apt install e2fsprogs zstd parted rsync

export BOOT_SIZE=256M
export ROOT_SIZE=2048M
export BOOT_UUID=44b7cb94-f58c-4ba6-bfa4-7d2dce09a3a5
export ROOT_UUID=80a5a8e9-c744-491a-93c1-4f4194fd690a
export ARCH=riscv
export CROSS_COMPILE=riscv64-linux-gnu-
export NPROC=12

all: disk.img

# Default user/password: root/archriscv

archriscv-latest.tar.zst:
	curl -O https://archriscv.felixc.at/images/archriscv-latest.tar.zst

linux:
	git clone -b fml-2025.12 https://github.com/s-holst/linux-stable.git linux

vmlinuz: linux
	make -C linux fml13v03_defconfig
	#sed -i 's/# CONFIG_SECURITY_LANDLOCK is not set/CONFIG_SECURITY_LANDLOCK=y/' linux/.config
	#sed -i 's/# CONFIG_SECURITY_YAMA is not set/CONFIG_SECURITY_YAMA=y/' linux/.config
	#make -C linux oldconfig
	make -C linux -j ${NPROC}
	cp -a linux/arch/riscv/boot/Image.gz vmlinuz

dtb: vmlinuz
	cp -a linux/arch/riscv/boot/dts/eswin/eic7702-deepcomputing-fml13v03.dtb dtb

modules: vmlinuz
	mkdir -p modules
	INSTALL_MOD_PATH=../modules make -C linux modules_install
	make -C linux headers_install

headers: vmlinuz
	mkdir -p headers/usr
	rsync -mrl --include='*/' --include='*\.h' --exclude='*' linux/usr/include headers/usr

busybox:
	git clone https://git.busybox.net/busybox

busybox/busybox: busybox
	make -C busybox defconfig
	sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' busybox/.config
	sed -i 's/CONFIG_TC=./CONFIG_TC=n/' busybox/.config
	make -C busybox -j ${NPROC}

# need linux for lpcpu firmware
initrd: busybox/busybox linux
	./scripts/make_initrd_bb.sh

boot.ext4: vmlinuz dtb initrd
	./scripts/make_boot_ext4.sh

root.ext4: archriscv-latest.tar.zst modules
	ROOT_TAR=archriscv-latest.tar.zst ./scripts/make_root_ext4.sh

disk.img: boot.ext4 root.ext4
	./scripts/make_disk_img.sh

enter:
	sudo SYSTEMD_NSPAWN_TMPFS_TMP=0 systemd-nspawn -D ./archriscv -a -U --setenv=LD_PRELOAD=/fix_tty.so
