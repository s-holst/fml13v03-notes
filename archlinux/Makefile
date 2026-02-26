# apt install e2fsprogs zstd parted rsync

export BOOT_SIZE=256M
export ROOT_SIZE=2048M
export BOOT_UUID=44b7cb94-f58c-4ba6-bfa4-7d2dce09a3a5
export ROOT_UUID=80a5a8e9-c744-491a-93c1-4f4194fd690a
export LINUX=fml13v03_linux
export ARCH=riscv
export CROSS_COMPILE=riscv64-linux-gnu-
export NPROC=6

all: disk.img

# Default user/password: root/archriscv

archriscv-latest.tar.zst:
	wget https://archriscv.felixc.at/images/archriscv-latest.tar.zst

fml13v03_linux:
	git clone -b fml13v03-6.6.92 https://github.com/DC-DeepComputing/fml13v03_linux.git

vmlinuz: fml13v03_linux
	make -C fml13v03_linux fml13v03_defconfig
	make -C fml13v03_linux -j ${NPROC}
	cp -a fml13v03_linux/arch/riscv/boot/Image.gz vmlinuz

dtb: vmlinuz
	cp -a fml13v03_linux/arch/riscv/boot/dts/eswin/eic7702-deepcomputing-fml13v03.dtb dtb

modules: vmlinuz
	mkdir -p modules/usr
	INSTALL_MOD_PATH=../modules make -C fml13v03_linux modules_install
	INSTALL_HDR_PATH=../modules/usr make -C fml13v03_linux headers_install

busybox:
	git clone https://git.busybox.net/busybox	

busybox/busybox: busybox
	make -C busybox defconfig
	sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' busybox/.config
	sed -i 's/CONFIG_TC=./CONFIG_TC=n/' busybox/.config
	make -C busybox -j ${NPROC}

initrd: busybox/busybox
	./scripts/make_initrd_bb.sh

boot.ext4: vmlinuz dtb initrd
	./scripts/make_boot_ext4.sh

root.ext4: archriscv-latest.tar.zst modules
	ROOT_TAR=archriscv-latest.tar.zst ./scripts/make_root_ext4.sh

disk.img: boot.ext4 root.ext4
	./scripts/make_disk_img.sh
