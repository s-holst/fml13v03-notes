Arch Linux Bootstrapping
========================

Prerequisites:
- A linux system (debian, ubuntu, arch, ...)
  - On MacOS, spin up a debian trixie with [OrbStack](https://orbstack.dev/download).
- Some required packages: gcc-14-riscv64-linux-gnu e2fsprogs zstd parted rsync wget git

A simple `make` will:
- download an [arch linux root fs tar](https://archriscv.felixc.at/images/)
- cross-compile the [linux kernel for fml13v03](https://github.com/DC-DeepComputing/fml13v03_linux.git)
- cross-compile busybox and create a minimal initrd
- does some light configuration on the root fs
- create bare-bones 2GB bootable `disc.img`

Flash the `disc.img` on a usb-stick and the framework laptop should automatically boot from it.


Things to do on first boot
--------------------------

Set a reasonable date for certificate validation `date -s 2026/2/26`

Set up wired network manually (no wifi without wpa_supplicant):
```
ip addr add 192.168.0.33/24 dev enu1u3
ip link set dev enu1u3 up
ip route add 192.168.0.1 dev enu1u3
ip route add default via 192.168.0.1 dev enu1u3
```

System update and install essentials:
```
pacman -Syu
pacman -S less vim networkmanager chrony
```

Enable wifi:
```
depmod
modprobe iwlwifi
systemctl start NetworkManager
systemctl enable NetworkManager
nmtui
```

Enable NTP client:
```
systemctl start chronyd.service
systemctl enable chronyd.service
```