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


First Boot TODOs
----------------

Generate locale, enable module loading, and reboot to load modules:
```
locale-gen
depmod
reboot
```
Use `lsmod` to check if modules were loaded.

Set up temporary wired network for system update and NetworkManager install:
```
ip addr add 192.168.0.33/24 dev enu1u3
ip link set dev enu1u3 up
ip route add 192.168.0.1 dev enu1u3
ip route add default via 192.168.0.1 dev enu1u3
```

Set a reasonable date for certificate validation `date -s 2026/2/26`

System update and install essentials:
```
pacman -Syu
pacman -S less vim networkmanager chrony openssh parted diffutils htop man-db
```

Configure wifi:
```
systemctl start NetworkManager
systemctl enable NetworkManager
nmtui
```

Enable NTP client. fml13v03 does not appear to have a battery-backed RTC for time-keeping when powered down.
Time needs to be set each time at boot via network. We also require chrony-wait service so that timer-based services like
archlinux keyring updates schedule as expected.
```
systemctl start chronyd
systemctl enable chronyd
systemctl enable chrony-wait
```

Enable SSH. Set `PermitRootLogin yes` in `/etc/ssh/sshd_config` if desired.
```
systemctl start sshd
systemctl enable sshd
```

Resize root partition to fill the flash drive:
```
parted /dev/sda resizepart 2 100%
partprobe /dev/sda
cat /sys/block/sda/sda2/size
resize2fs /dev/sda2
```

Sensor Monitoring
```
pacman -S lm_sensors rrdtool
systemctl start sensord
systemctl enable sensord
```

User config
```
useradd -m roma
passwd roma
```


Things that work out-of-the-box:
- Fn-Space toggles through keyboard backlight brightnesses: low, mid, bright, off
- Power Button sends KEY_POWER on /dev/input/event6, initiates shutdown
- Airplane(F10) sends KEY_RFKILL on /dev/input/event6, toggles wifi (rf-kill) and bluetooth (usb disconnect device), 
- Gear(F12) sends KEY_MEDIA on /dev/input/event6
- Other media control keys and brightness control send on /dev/input/event2
- "fn lock" toggles between Fn first / media keys first. 
- Camera switch disconnects camera USB device

`evtest /dev/input/event*` to monitor events
- event0: "DeepComputing DC keyboard" keyboard including Fxx-keys (no Fn)
- event1: "DeepComputing DC keyboard System Control" emits KEY_SWITCHVIDEOMODE Monitor(F9)
- event2: "DeepComputing DC keyboard Consumer Control" emits all media control and brightness keys F1-F8
- event3: "hid-over-i2c 093A:0274 Mouse" (no events on factory ubuntu)
- event4: "hid-over-i2c 093A:0274 Touchpad" emits touchpad events
- event5: "ES8326 Audio Headphone" SW_HEADPHONE_INSERT SW_MICROPHONE_INSERT, can distinguish between jacks with and widthout mic.
- event6: "cros_ec_buttons" emits KEY_MEDIA Gear(F12) KEY_RFKILL Airplane(F10) KEY_POWER PowerButton, KEY_SW lid sensor.

Ambient light sensor Capella CM32183 (I2C address 0x29 on bus 2):
- `cat /sys/bus/iio/devices/iio:device0/in_illuminance_input` in lux

Backlight control (PWM backlight 0...254, default 128):
- `echo 50  > /sys/class/backlight/backlight/brightness`

Sensors and fan control:
- Temp, voltage, rpm readings via lm_sensors.
- Fan current rpm: /sys/class/hwmon/hwmon8/fan1_input
- Fan speed control (PWM duty 0..255): /sys/class/hwmon/hwmon8/pwm1
- No temp dependent fan control out-of-the-box. Only freq throttling by power_allocator (kicks in at 65C).

Frequency control:
- Die0: /sys/devices/system/cpu/cpu0/cpufreq (700 800 900 1400 1500 1600 1700 1800 MHz)
- Die1: /sys/devices/system/cpu/cpu4/cpufreq (700 800 900 1400 1500 1600 1700 1800 MHz)
- Available governors: conservative ondemand userspace powersave *performance* schedutil
- Die0 NPU: /sys/class/devfreq/51c00000.eswin-npu (520 750 1040 1188 1500 MHz)
- Die1 NPU: /sys/class/devfreq/71c00000.eswin-npu (520 750 1040 1188 1500 MHz)
- GPU /sys/class/devfreq/51400000.gpu (200 400 800)
- Die0 DSPs: /sys/class/devfreq/52280400.dsp_subsys:es_dsp@{0,1,2,3} (520 1040 MHz)
- Die1 DSPs: /sys/class/devfreq/72280400.dsp_subsys:es_dsp@{0,1,2,3} (520 1040 MHz)
- Available governors: userspace performance *simple_ondemand*

Everything to slowest clock:
```
echo 700000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 700000 >/sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 520000000 >/sys/class/devfreq/51c00000.eswin-npu/max_freq
echo 520000000 >/sys/class/devfreq/71c00000.eswin-npu/max_freq
echo 200000000 >/sys/class/devfreq/51400000.gpu/max_freq
echo 520000000 >/sys/class/devfreq/52280400.dsp_subsys:es_dsp@0/max_freq
echo 520000000 >/sys/class/devfreq/52280400.dsp_subsys:es_dsp@1/max_freq
echo 520000000 >/sys/class/devfreq/52280400.dsp_subsys:es_dsp@2/max_freq
echo 520000000 >/sys/class/devfreq/52280400.dsp_subsys:es_dsp@3/max_freq
echo 520000000 >/sys/class/devfreq/72280400.dsp_subsys:es_dsp@0/max_freq
echo 520000000 >/sys/class/devfreq/72280400.dsp_subsys:es_dsp@1/max_freq
echo 520000000 >/sys/class/devfreq/72280400.dsp_subsys:es_dsp@2/max_freq
echo 520000000 >/sys/class/devfreq/72280400.dsp_subsys:es_dsp@3/max_freq
```

Sound
```
pacman -S alsa-utils
```
audio (headphone detect, mute switch, speaker)



Graphical

```
pacman -S sway foot swaybg wmenu lightdm lightdm-gtk-greeter

```

sway recommended
```
brightnessctl: Brightness adjustment tool used in the default configuration
foot: Terminal emulator used in the default configuration
grim: Screenshot utility used in the default configuration
i3status: Status line generation
libpulse: Volume adjustment tool (pactl) used in the default configuration
mako: Lightweight notification daemon
polkit: System privilege control. Required if not using seatd service [installed]
sway-contrib: Collection of user-contributed scripts for sway
swayidle: Idle management daemon
swaylock: Screen locker
waybar: Highly customizable bar
xorg-xwayland: X11 support
xdg-desktop-portal-gtk: Default xdg-desktop-portal for file picking
xdg-desktop-portal-wlr: xdg-desktop-portal backend
```


These modules are loaded in ubuntu only:

```
af_alg                 40960  6 algif_hash,algif_skcipher
algif_hash             16384  1
algif_skcipher         16384  1

binfmt_misc

dax                    57344  1 dm_mod
dm_mod                245760  0

nls_iso8859_1          16384  1
qrtr                   65536  2
rfcomm      

snd_hrtimer            12288  1
snd_seq               139264  7 snd_seq_dummy
snd_seq_dummy

x_tables
ip_tables
```


warning on headphone disconnect:

```
[  482.524306] ------------[ cut here ]------------
[  482.528982] d0_clk_vo_i2s_mclk already disabled
[  482.533649] WARNING: CPU: 1 PID: 55 at drivers/clk/clk.c:1181 clk_core_disable+0x144/0x150
[  482.533671] Modules linked in: eic7700_npu cros_usbpd_charger cros_ec_sysfs cros_sbs_battery cros_ec_chardev cros_usbpd_logger cros_usbpd_notify cros_ec_hwmon cros_ec_poweroff cros_ec_dev mfd_core cros_ec_keyb matrix_keymap btusb btmtk btrtl btbcm btintel bluetooth ecdh_generic uvcvideo iwlmvm ecc iwlwifi eic7700_dsp cros_ec_spi cros_ec joydev uio_pdrv_genirq uio sch_fq_codel fuse nfnetlink ipv6 crc_ccitt autofs4
[  482.533757] CPU: 1 PID: 55 Comm: kworker/u18:0 Not tainted 6.6.92-g417741216c08-dirty #6
[  482.533762] Hardware name: DeepComputing FML13V03 (DT)
[  482.533764] Workqueue: events_unbound async_run_entry_fn
[  482.533782] epc : clk_core_disable+0x144/0x150
[  482.533788]  ra : clk_core_disable+0x144/0x150
[  482.533793] epc : ffffffff806992c6 ra : ffffffff806992c6 sp : ffff8f8003413c90
[  482.533796]  gp : ffffffff823dfe78 tp : ffffaf80c9101d00 t0 : 0000000000000000
[  482.533799]  t1 : ffffaf9fcec30ff0 t2 : 0000000000000000 s0 : ffff8f8003413cb0
[  482.533802]  s1 : ffffaf80c909e600 a0 : 0000000000000023 a1 : ffffffff82289680
[  482.533805]  a2 : 0000000200000020 a3 : ffffffff82850818 a4 : 0000000000000000
[  482.533808]  a5 : 0000000000000000 a6 : 0000000000000001 a7 : 0000000000000001
[  482.533811]  s2 : ffffaf80c909e600 s3 : 0000000000000000 s4 : 0000000000000001
[  482.533813]  s5 : ffffaf80cd8f7e28 s6 : ffffaf80c718ee05 s7 : ffffaf80c707c600
[  482.533816]  s8 : ffffaf80c706b800 s9 : 0000000000000000 s10: 0000000000000000
[  482.533819]  s11: 0000000000000000 t3 : ffffffffffffffff t4 : 0000000000000001
[  482.533821]  t5 : 0000000000000008 t6 : ffff8f8003413aa8
[  482.533824] status: 0000000200000100 badaddr: 0000000000000000 cause: 0000000000000003
[  482.533827] [<ffffffff806992c6>] clk_core_disable+0x144/0x150
[  482.533833] [<ffffffff806992f6>] clk_disable+0x24/0x38
[  482.533839] [<ffffffff80d255dc>] es8326_set_bias_level+0x34/0x22a
[  482.533849] [<ffffffff80d1b25c>] snd_soc_component_set_bias_level+0x14/0x62
[  482.533862] [<ffffffff80d14b9e>] snd_soc_dapm_set_bias_level+0x44/0xfe
[  482.533866] [<ffffffff80d14d20>] dapm_post_sequence_async+0x46/0xc2
[  482.533870] [<ffffffff8004867a>] async_run_entry_fn+0x2a/0x128
[  482.533875] [<ffffffff8003ad0e>] process_one_work+0x11c/0x2fc
[  482.533883] [<ffffffff8003b15c>] worker_thread+0x26e/0x352
[  482.533888] [<ffffffff80043414>] kthread+0xc4/0xde
[  482.533892] [<ffffffff81050796>] ret_from_fork+0xe/0x18
[  482.533901] ---[ end trace 0000000000000000 ]---
[  543.903918] ------------[ cut here ]------------
```