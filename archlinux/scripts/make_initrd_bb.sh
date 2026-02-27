#!/usr/bin/env bash
set -euo pipefail

WORK_DIR="$(pwd)/tmp"

rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

cd "${WORK_DIR}"
mkdir -p {bin,sbin,dev,etc,home,mnt,proc,sys,usr,tmp}
mkdir -p usr/{bin,sbin}
mkdir -p proc/sys/kernel

cd dev
sudo mknod sda b 8 0 
sudo mknod console c 5 1
cd ..

cp ../busybox/busybox ./bin/
cat <<EOF >init
#!/bin/busybox sh
/bin/busybox --install -s
mount -t devtmpfs  devtmpfs  /dev
mount -t proc      proc      /proc
mount -t sysfs     sysfs     /sys
mount -t tmpfs     tmpfs     /tmp

#echo /sbin/mdev > /proc/sys/kernel/hotplug
#mdev -s

RWFLAG=-w
INIT=/sbin/init

for x in \$(cat /proc/cmdline); do
	case \$x in
	init=*)
		INIT=\${x#init=}
		;;
	root=*)
		ROOT=\${x#root=}
		;;
    ro)
		RWFLAG=-r
		;;
	rw)
		RWFLAG=-w
		;;
    esac
done

mount \$RWFLAG \$ROOT /mnt

# Move virtual filesystems over to the real filesystem
mount -n -o move /sys /mnt/sys
mount -n -o move /proc /mnt/proc
mount -n -o move /tmp /mnt/tmp
mount -t devtmpfs -o nosuid,mode=0755 udev /mnt/dev

#mount -n -o move /dev /mnt/dev
#rm -fr /dev
#ln -s /mnt/dev /dev

exec run-init "/mnt" "\${INIT}" "\$@" <"/dev/console" >"/dev/console" 2>&1
#setsid cttyhack sh

EOF

chmod +x init

find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initrd

cd ..

exit 0
#############


# Prepare the /dev directory
#[ ! -h /dev/fd ] && ln -s /proc/self/fd /dev/fd
#[ ! -h /dev/stdin ] && ln -s /proc/self/fd/0 /dev/stdin
#[ ! -h /dev/stdout ] && ln -s /proc/self/fd/1 /dev/stdout
#[ ! -h /dev/stderr ] && ln -s /proc/self/fd/2 /dev/stderr

#mkdir /dev/pts
#mount -t devpts -o noexec,nosuid,gid=5,mode=0620 devpts /dev/pts || true

RWFLAG=-w
INIT=/sbin/init

for x in $(cat /proc/cmdline); do
	case $x in
	init=*)
		INIT=${x#init=}
		;;
	root=*)
		ROOT=${x#root=}
		;;
    ro)
		RWFLAG=-r
		;;
	rw)
		RWFLAG=-w
		;;
    esac
done

mount $RWFLAG $ROOT /mnt

# Move virtual filesystems over to the real filesystem
mount -n -o move /sys /mnt/sys
mount -n -o move /proc /mnt/proc
mount -n -o move /tmp /mnt/tmp
mount -t devtmpfs devtmpfs /mnt/dev

sh
#exec run-init "/mnt" "${INIT}" "$@" <"/mnt/dev/console" >"/mnt/dev/console" 2>&1
