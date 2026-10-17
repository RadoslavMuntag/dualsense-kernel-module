# Working notes

## Device files: block and character devices

On the host:

```
❯ ls -lh /dev/nvme0n1 /dev/ttyS0 
Permissions major,minor Size User Date Modified Name
brw-rw----  259,0 root  7 Oct 21:43  󰡯 /dev/nvme0n1     # block device
crw-rw----   4,64 root  7 Oct 21:43  󰡯 /dev/ttyS0       # character device
```
`/proc/devices` - device number to driver mapping

## Phase 2 - loading module with configurable parameter

### Commands

```
ubuntu@kmod-dev:~/ds_demo_mod$ make
make -C /lib/modules/6.8.0-146-generic/build M=/home/ubuntu/ds_demo_mod modules
make[1]: Entering directory '/usr/src/linux-headers-6.8.0-146-generic'
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: x86_64-linux-gnu-gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
  You are using:           gcc-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
  CC [M]  /home/ubuntu/ds_demo_mod/ds_demo.o
  MODPOST /home/ubuntu/ds_demo_mod/Module.symvers
  CC [M]  /home/ubuntu/ds_demo_mod/ds_demo.mod.o
  LD [M]  /home/ubuntu/ds_demo_mod/ds_demo.ko
  BTF [M] /home/ubuntu/ds_demo_mod/ds_demo.ko
Skipping BTF generation for /home/ubuntu/ds_demo_mod/ds_demo.ko due to unavailability of vmlinux
make[1]: Leaving directory '/usr/src/linux-headers-6.8.0-146-generic'

ubuntu@kmod-dev:~/ds_demo_mod$ modinfo ds_demo.ko
filename:       /home/ubuntu/ds_demo_mod/ds_demo.ko
version:        0.2
description:    A sample driver for dualsense controller
author:         Radoslav Muntag
license:        GPL
srcversion:     C7CC8D714BE7042E10A23D5
depends:        
retpoline:      Y
name:           ds_demo
vermagic:       6.8.0-146-generic SMP preempt mod_unload modversions 
parm:           blink_ms:Tick interval in ms (100..10000, default 1000)

ubuntu@kmod-dev:~/ds_demo_mod$ sudo insmod ds_demo.ko
ubuntu@kmod-dev:~/ds_demo_mod$ sudo rmmod ds_demo
ubuntu@kmod-dev:~/ds_demo_mod$ sudo insmod ds_demo.ko blink_ms=5
insmod: ERROR: could not insert module ds_demo.ko: Invalid parameters

ubuntu@kmod-dev:~/ds_demo_mod$ sudo insmod ds_demo.ko blink_ms=300
ubuntu@kmod-dev:~/ds_demo_mod$ sudo rmmod ds_demo
ubuntu@kmod-dev:~/ds_demo_mod$ sudo insmod ds_demo.ko blink_ms=5000
ubuntu@kmod-dev:~/ds_demo_mod$ sudo insmod ds_demo.ko blink_ms=5000
insmod: ERROR: could not insert module ds_demo.ko: File exists

ubuntu@kmod-dev:~/ds_demo_mod$ sudo rmmod ds_demo

```

### Logs

Long runs of identical tick lines are shortened.

```
ubuntu@kmod-dev:~$ sudo dmesg -WT
[Sun Oct 11 14:55:42 2026] ds_demo: loading out-of-tree module taints kernel.
[Sun Oct 11 14:55:42 2026] ds_demo: module verification failed: signature and/or required key missing - tainting kernel
[Sun Oct 11 14:55:42 2026] ds_demo: loaded, blink_ms=1000
[Sun Oct 11 14:55:43 2026] ds_demo: tick 1: LED would be ON (simulated, blink_ms=1000)
[Sun Oct 11 14:55:44 2026] ds_demo: tick 2: LED would be OFF (simulated, blink_ms=1000)
[Sun Oct 11 14:55:45 2026] ds_demo: tick 3: LED would be ON (simulated, blink_ms=1000)
... (23 more ticks) ...
[Sun Oct 11 14:56:10 2026] ds_demo: tick 27: LED would be ON (simulated, blink_ms=1000)
[Sun Oct 11 14:56:10 2026] ds_demo: unloaded after 27 ticks
[Sun Oct 11 14:57:38 2026] ds_demo: blink_ms=5 rejected, allowed range 100..10000
[Sun Oct 11 14:57:38 2026] ds_demo: `5' invalid for parameter `blink_ms'
[Sun Oct 11 14:57:54 2026] ds_demo: blink_ms set to 300
[Sun Oct 11 14:57:54 2026] ds_demo: loaded, blink_ms=300
[Sun Oct 11 14:57:54 2026] ds_demo: tick 1: LED would be ON (simulated, blink_ms=300)
[Sun Oct 11 14:57:54 2026] ds_demo: tick 2: LED would be OFF (simulated, blink_ms=300)
[Sun Oct 11 14:57:55 2026] ds_demo: tick 3: LED would be ON (simulated, blink_ms=300)
... (26 more ticks) ...
[Sun Oct 11 14:58:03 2026] ds_demo: tick 30: LED would be OFF (simulated, blink_ms=300)
[Sun Oct 11 14:58:03 2026] ds_demo: unloaded after 30 ticks
[Sun Oct 11 14:58:17 2026] ds_demo: blink_ms set to 5000
[Sun Oct 11 14:58:17 2026] ds_demo: loaded, blink_ms=5000
[Sun Oct 11 14:58:22 2026] ds_demo: tick 1: LED would be ON (simulated, blink_ms=5000)
[Sun Oct 11 14:58:28 2026] ds_demo: tick 2: LED would be OFF (simulated, blink_ms=5000)
[Sun Oct 11 14:58:33 2026] ds_demo: tick 3: LED would be ON (simulated, blink_ms=5000)
[Sun Oct 11 14:58:38 2026] ds_demo: tick 4: LED would be OFF (simulated, blink_ms=5000)
[Sun Oct 11 14:58:40 2026] ds_demo: unloaded after 4 ticks
```
