# Command log

A record of the commands run while setting up this VM, in order, with what
each was for and what it showed. Long one-line compound commands are split
into their parts here; the commands themselves are unchanged.

Commands marked **(you)** needed root and were run by you, not by Claude.

## 1. Investigating the host (2026-10-06)

Does the CPU support virtualization, and is KVM usable?

```
grep -Eoc '(vmx|svm)' /proc/cpuinfo
ls -l /dev/kvm
```

Result: 24 matches, and `/dev/kvm` exists with mode `crw-rw-rw-`, so KVM works
without root.

Which VM tools are installed?

```
for t in qemu-system-x86_64 qemu-img virsh virt-install virt-manager VBoxManage \
         gnome-boxes multipass vagrant quickemu cloud-localds genisoimage xorriso \
         docker incus lxc; do printf '%s: ' $t; command -v $t || echo no; done
```

Result: only `docker`. No hypervisor.

Who am I, and how much room is there?

```
id
nproc
free -h | head -2
df -h /home/namelessnovice | tail -1
ls -la /home/namelessnovice/Work
ls ~/.ssh/*.pub
```

Result: user in group `wheel`; 12 cores; 15 GiB RAM with about 10 GiB
available; 283 GB free disk; an `id_ed25519` SSH key exists.

Can I install packages, and what can build the cloud-init image?

```
sudo -n true
pacman -Si qemu-base
pacman -Si libisoburn
for t in mkfs.vfat mcopy mkisofs bsdtar curl wget; do printf '%s: ' $t; command -v $t || echo no; done
ss -ltn | grep -c ':2222 '
```

Result: `sudo` needs a password, so installing was left to you. `qemu-base`
11.1.1 is in the repos. `bsdtar` is present and can write ISO images, so no
extra ISO tool was needed. Port 2222 is free.

## 2. Downloading Ubuntu (2026-10-06)

```
mkdir -p /home/namelessnovice/Work/ubuntu-vm
cd /home/namelessnovice/Work/ubuntu-vm
curl -fsSL -o SHA256SUMS https://cloud-images.ubuntu.com/noble/current/SHA256SUMS
curl -fL --retry 3 -sS -o noble-server-cloudimg-amd64.img \
  https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
grep ' \*\?noble-server-cloudimg-amd64.img$' SHA256SUMS | sha256sum -c -
```

Result: `noble-server-cloudimg-amd64.img: OK` (597 MB).

## 3. Writing and checking the script (2026-10-06)

The `vm` script was written, then checked:

```
chmod +x vm
bash -n vm
```

A test seed image was built to confirm cloud-init would recognise it:

```
bsdtar -cf seed.iso --format=iso9660 --options 'iso9660:volume-id=CIDATA' -C $t user-data meta-data
blkid seed.iso
bsdtar -tf seed.iso
```

Result: `LABEL="CIDATA" TYPE="iso9660"`, containing both files.

```
./vm status
./vm start
```

Result: `stopped`, then `QEMU is not installed. Run: sudo pacman -S qemu-base`.

## 4. Installing QEMU **(you)**

```
sudo pacman -S qemu-base
```

## 5. First boot and verification (2026-10-06)

```
./vm start
tail -5 console.log
```

Result: SSH came up; the console showed cloud-init running its final stage.

```
./vm ssh 'cloud-init status --wait; lsb_release -ds; uname -r;
          ls -d /lib/modules/$(uname -r)/build; ls /var/lib/cloud/kmod-dev-ready;
          gcc --version | head -1; make --version | head -1;
          nproc; free -h | sed -n 2p; df -h / | tail -1'
```

Result:

| Check | Result |
|---|---|
| cloud-init | `status: done` |
| OS | Ubuntu 24.04.5 LTS |
| Kernel | 6.8.0-142-generic |
| Headers | `/lib/modules/6.8.0-142-generic/build` present |
| Compiler | gcc 13.3.0, GNU Make 4.3 |
| Resources | 4 CPUs, 3.8 GiB RAM, 29 GB disk with 26 GB free |

## 6. Adding snapshots (2026-10-10)

Looking at the current state before changing anything:

```
ls -lh
./vm status
grep -n 'cmd_\|^case\|^  [a-z]*)' vm
qemu-img snapshot -U -l disk.qcow2
command -v socat nc ncat
```

Result: VM stopped, no snapshots yet.

The snapshot code was tested on a throwaway copy of the VM in a temporary
directory, on port 2223, so the real disk was never touched. That directory
has since been deleted.

```
cp /home/namelessnovice/Work/ubuntu-vm/vm .
ln -s /home/namelessnovice/Work/ubuntu-vm/noble-server-cloudimg-amd64.img .
export VM_SSH_PORT=2223
bash -n vm
```

First run:

```
./vm snapshot list                 # before any disk exists
./vm start
./vm snapshot list
./vm ssh 'touch ~/A; sync'
./vm snapshot create base
./vm ssh 'touch ~/B; sync; ls ~'
./vm snapshot create base          # duplicate name
./vm status
./vm snapshot create               # default timestamp name
./vm snapshot list
./vm snapshot revert nope          # missing snapshot
./vm snapshot revert base
./vm ssh 'echo after-revert:; ls ~'
./vm stop
./vm snapshot delete base
./vm snapshot list
./vm status
```

Result: create, list and delete worked. One bug showed up: creating a
snapshot under an existing name shut the VM down before failing, and left it
stopped. The check was moved ahead of the shutdown.

Second run, after the fix:

```
./vm start
./vm ssh 'ls ~; touch ~/C; sync'
./vm snapshot create withC
./vm snapshot create withC         # duplicate: now fails with the VM still running
./vm status
./vm snapshot create 'bad name'    # rejected
./vm ssh 'touch ~/D; sync; echo before-revert:; ls ~'
./vm snapshot revert withC         # revert while running
./vm ssh 'echo after-revert:; ls ~'
./vm snapshot delete withC
./vm status
./vm snapshot list
./vm snapshot bogus                # usage error
./vm stop
```

Result: before the revert the guest had files `A C D`; after it, `A C`. The
VM came back up by itself after create, revert and delete.

## 7. Investigating the DualSense controller (2026-10-10)

On the host:

```
lsusb
```

Result: `Bus 001 Device 012: ID 054c:0ce6 Sony Corp. DualSense wireless controller (PS5)`.

Details from sysfs, for every device with Sony's vendor ID:

```
for d in /sys/bus/usb/devices/*; do
  [ -f $d/idVendor ] && [ "$(cat $d/idVendor)" = 054c ] &&
    echo "$d $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product) bus=$(cat $d/busnum) dev=$(cat $d/devnum) speed=$(cat $d/speed)" &&
    ls -l /dev/bus/usb/$(printf '%03d/%03d' $(cat $d/busnum) $(cat $d/devnum)) &&
    for i in $d:*; do
      echo "  $(basename $i) class=$(cat $i/bInterfaceClass) driver=$(basename $(readlink $i/driver))"
    done
done
```

Result:

```
/sys/bus/usb/devices/1-2.2 054c:0ce6 DualSense Wireless Controller bus=1 dev=12 speed=480
crw-rw-r-- 1 root root 189, 11 /dev/bus/usb/001/012
  1-2.2:1.0 class=01 driver=snd-usb-audio
  1-2.2:1.1 class=01 driver=snd-usb-audio
  1-2.2:1.2 class=01 driver=snd-usb-audio
  1-2.2:1.3 class=03 driver=usbhid
```

The device node is writable only by root, which is why a udev rule is needed
for passthrough.

Which HID driver sits on top, and is the controller also paired over
Bluetooth?

```
ls /sys/bus/hid/devices/ | grep -i 054C
lsmod | grep -i -E 'playstation|usbhid'
bluetoothctl devices
ls /etc/udev/rules.d/
```

Result: HID device `0003:054C:0CE6.0027`; `hid_playstation` is loaded, with
`ff_memless` and `led_class_multicolor` as its dependencies. The controller is
not paired over Bluetooth. One unrelated udev rule file exists.

In the guest:

```
./vm ssh 'uname -r;
  for m in hid_playstation usbhid hid_generic xhci_pci snd_usb_audio; do
    printf "%s: " $m; modinfo -n $m; done;
  grep -E "CONFIG_(USB_XHCI_HCD|USB_HID|HID_PLAYSTATION|HID_GENERIC|USB_XHCI_PCI)=" /boot/config-$(uname -r);
  dpkg -l | grep -E "linux-(modules|image)";
  lsusb | head -3; ls ~'
```

Result: `usbhid`, `hid_generic` and `xhci_pci` are available;
`hid_playstation` and `snd_usb_audio` are not. The kernel was configured with
`CONFIG_HID_PLAYSTATION=m`, but that module ships in `linux-modules-extra`,
which is not installed. Only `linux-modules-6.8.0-142-generic` is.

Does this QEMU build support passthrough?

```
qemu-system-x86_64 -device help | grep -E '"(usb-host|qemu-xhci|usb-kbd|usb-redir)"'
pacman -Q | grep -E '^qemu'
pacman -Si qemu-hw-usb-host
cat /etc/udev/rules.d/70-darkproject.rules
ls /usr/lib/udev/rules.d | grep -E 'seat|uaccess|steam|sony|dualsense'
```

Result: `qemu-xhci` and `usb-kbd` are available, `usb-host` is not. It comes
from the separate package `qemu-hw-usb-host`. The existing rule file shows the
`TAG+="uaccess"` pattern used on this system for granting device access.

## 8. Adding the USB controller (2026-10-10)

The script was edited to add `-device qemu-xhci` and the `usb-host`
passthrough options, and `70-dualsense-vm.rules` was written. Then:

```
bash -n vm
grep -n 'USB' vm | head -30
```

Tested on another throwaway copy, port 2223:

```
./vm start
./vm ssh 'for d in /sys/bus/usb/devices/usb*; do
            echo "$(basename $d): $(cat $d/product) / $(cat $d/manufacturer)"; done;
          lspci | grep -i usb'
./vm stop
VM_USB="" ./vm start
./vm stop
```

Result:

```
warning: no USB passthrough support in QEMU; run: sudo pacman -S qemu-hw-usb-host
usb1: xHCI Host Controller / Linux 6.8.0-142-generic xhci-hcd
usb2: xHCI Host Controller / Linux 6.8.0-142-generic xhci-hcd
00:03.0 USB controller: Red Hat, Inc. QEMU XHCI Host Controller (rev 01)
```

The guest sees the controller. With `VM_USB=""` the warning does not appear.
Passthrough of the DualSense itself was not tested at this point.

## 9. Enabling passthrough **(you)**

```
sudo pacman -S qemu-hw-usb-host
sudo install -m 644 ~/Work/ubuntu-vm/70-dualsense-vm.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --attr-match=idVendor=054c
./vm stop
./vm start
```

## 10. Verifying passthrough (2026-10-10)

On the host:

```
qemu-system-x86_64 -device help | grep -c '"usb-host"'
ls -l /etc/udev/rules.d/70-dualsense-vm.rules
ls -l /dev/bus/usb/001/012
getfacl -p /dev/bus/usb/001/012 | grep user:
tr '\0' ' ' < /proc/$(cat qemu.pid)/cmdline | grep -o -E 'qemu-xhci|usb-host[^ ]*'
```

Result: `usb-host` is available, the rule is installed, and the device node
has an access entry `user:namelessnovice:rw-`. All four interfaces of the
controller show `driver=usbfs`, meaning QEMU has claimed the device. QEMU is
running with `qemu-xhci` and `usb-host,vendorid=0x054c,productid=0x0ce6`.

In the guest:

```
./vm ssh 'for d in /sys/bus/usb/devices/*; do
            [ -f $d/idVendor ] && echo "$(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product)"; done;
          for i in /sys/bus/usb/devices/*:*; do
            echo "  $(basename $i) class=$(cat $i/bInterfaceClass) driver=$(basename "$(readlink $i/driver)")"; done;
          ls /sys/bus/hid/devices/; ls /dev/hidraw* /dev/input/js*;
          sudo dmesg | grep -i -E "054c|dualsense|hid-generic|playstation" | tail -8'
```

Result:

```
1-1 054c:0ce6 DualSense Wireless Controller
  1-1:1.0 class=01 driver=
  1-1:1.1 class=01 driver=
  1-1:1.2 class=01 driver=
  1-1:1.3 class=03 driver=usbhid
0003:054C:0CE6.0001
/dev/hidraw0
/dev/input/js0
hid-generic 0003:054C:0CE6.0001: input,hidraw0: USB HID v1.11 Gamepad [Sony Interactive Entertainment DualSense Wireless Controller] on usb-0000:00:03.0-1/input3
```

Is live data arriving?

```
./vm ssh 'sudo timeout 3 head -c 128 /dev/hidraw0 | od -A d -t x1;
          basename $(readlink /sys/bus/hid/devices/0003:054C:0CE6.0001/driver)'
```

Result: two 64-byte reports, each starting `01 7f 7f 81 7e`. The HID driver is
`hid-generic`.

## 11. Adding `./vm sync` (2026-10-10)

Reading the script before editing it:

```
sed -n 1,32p vm
grep -n 'cmd_reset\|^case\|reset)\|snapshot)\|snapshot delete\|create/revert/delete' vm
./vm status
```

Result: the script changes into its own directory at the top, so a relative
path given to a new command would resolve from the wrong place. The fix was to
remember the caller's directory first. The VM was running.

After adding the `sync` command, a throwaway directory was created on the host.
Its name contains a space on purpose:

```
bash -n vm
T=$(mktemp -d)
mkdir -p "$T/sync test/sub"
cd "$T"
printf 'int x;\n' > "sync test/hello.c"
echo 'obj-m += hello.o' > "sync test/Makefile"
echo n > "sync test/sub/notes.txt"
```

First sync, into `~/vmsync-selftest` in the real VM:

```
./vm sync "sync test" vmsync-selftest
```

Result:

```
created directory vmsync-selftest
cd+++++++++ ./
<f+++++++++ Makefile
<f+++++++++ hello.c
cd+++++++++ sub/
<f+++++++++ sub/notes.txt
```

Fake build products and a stale source file were then created in the guest,
the host copy was edited, and the directory was synced again:

```
./vm ssh 'touch vmsync-selftest/hello.o vmsync-selftest/hello.ko vmsync-selftest/stale.c; ls -A vmsync-selftest'
echo 'int y;' >> "sync test/hello.c"
rm -r "sync test/sub"
./vm sync "sync test" vmsync-selftest
./vm ssh 'ls -A vmsync-selftest; cat vmsync-selftest/hello.c'
```

Result:

```
*deleting   sub/notes.txt
*deleting   sub/
*deleting   stale.c
<f.st...... hello.c
```

The guest directory then held `Makefile hello.c hello.ko hello.o`, and
`hello.c` had both lines. So the edit arrived, the files removed on the host
and the stale guest file were deleted, and the `.o` and `.ko` files were left
alone.

Error handling:

```
./vm sync                          # no argument
./vm sync nope                     # path does not exist
./vm sync "sync test" '../x'       # unsafe guest name
./vm sync "sync test/hello.c"      # a file, not a directory
cd "sync test" && ./vm sync .      # relative path; default name has a space
```

Result: each was refused with a message and nothing was transferred. The last
one shows that `.` resolved from the caller's directory to `sync test`, which
was then rejected as a guest name because of the space.

Cleaning up on both sides:

```
./vm ssh 'rm -rf vmsync-selftest "sync test"; ls -A ~ | head'
rm -rf "$T"
```

Result: the guest home directory held only its original dotfiles.

## 12. Fixing clangd on the host (2026-10-11)

clangd reported every line as an error. Looking for the cause:

```
clangd --version
ls /usr/lib/modules/
pacman -Q linux-headers sparse bear
find . -maxdepth 3 -name compile_commands.json -o -name .clangd -o -name compile_flags.txt
```

Result: clangd 22.1.8. The host has headers only for its own Arch kernel
(7.2), and there was no clangd configuration, so no Ubuntu kernel header could
be found.

What does the guest have, and what flags does the real build use?

```
./vm ssh 'uname -r; du -sh /usr/src/linux-headers-*;
          ls /usr/src/linux-headers-$(uname -r)/scripts/checkpatch.pl;
          cat ~/hello_mod/.hello.o.cmd | head -1'
```

Result: the guest was now on `6.8.0-146-generic`. Headers come as two
directories, a per-flavour one (29 MB) that links into a shared one (128 MB).
The `.hello.o.cmd` file holds the full gcc command line, which supplied the
include paths and defines for `.clangd`.

Copying the headers out (now done by `./vm headers`) and checking:

```
rsync -a --delete -e "ssh -p 2222 ..." \
  ubuntu@127.0.0.1:/usr/src/linux-headers-6.8.0-146-generic \
  ubuntu@127.0.0.1:/usr/src/linux-headers-6.8.0-146 kernel-headers/
ln -sfn linux-headers-6.8.0-146-generic kernel-headers/current
clangd --check=hello_mod/hello.c
clangd --check=ds_demo_mod/ds_demo.c
```

Result: `All checks completed, 0 errors` for both files. A test file calling an
undeclared function gave 1 error, so real mistakes are still caught.

The kernel style checker, run from the same copy:

```
perl kernel-headers/current/scripts/checkpatch.pl --no-tree --terse -f hello_mod/hello.c
perl kernel-headers/current/scripts/checkpatch.pl --no-tree --terse -f ds_demo_mod/ds_demo.c
```

Result: `hello.c` 0 errors and 5 warnings; `ds_demo.c` 4 errors and 13
warnings, nearly all about spaces being used for indentation where kernel style
wants tabs.

## 13. Investigating the kernel version change (2026-10-11)

```
./vm ssh 'uname -r;
          dpkg -l | grep -E "linux-(image|headers|modules)";
          for d in /lib/modules/*; do echo "$d build=$(readlink $d/build)"; done;
          for k in ~/*/*.ko; do echo "$k $(modinfo -F vermagic $k)"; done;
          grep -E "^(Start-Date|Commandline|Install|Upgrade):" /var/log/apt/history.log | grep -B2 -E "linux-(image|headers)"'
```

Result: running `6.8.0-146-generic`, with `142` and `146` kernels and headers
both installed. `ds_demo.ko` was built for `146`, `hello.ko` for `142`. The apt
history shows `/usr/bin/unattended-upgrade` installing `146` on 2026-10-10 at
14:21 UTC.

Which kernel did each boot use?

```
./vm ssh 'journalctl --list-boots --no-pager -q;
          for b in -7 -6 -5 -4 -3 -2 -1 0; do
            sudo journalctl -b $b --no-pager -q -o short-iso | grep -m1 -o "Linux version [^ ]*"; done;
          cat /etc/apt/apt.conf.d/20auto-upgrades;
          systemctl list-timers apt-daily.timer apt-daily-upgrade.timer --no-pager'
```

Result: every boot up to 2026-10-10 13:53 UTC ran `142`. The first boot on
`146` was 2026-10-10 21:23 UTC, the first one after the install.
`APT::Periodic::Unattended-Upgrade "1"` confirms the updater is enabled.

The kernel packages were then held **(you)**:

```
sudo apt-mark hold linux-virtual linux-image-virtual linux-headers-virtual linux-headers-generic
```

## 14. Checking the README commands (2026-10-11)

Host and guest copies of the sources were compared before syncing:

```
./vm ssh 'md5sum ~/ds_demo_mod/ds_demo.c ~/hello_mod/hello.c'
md5sum ds_demo_mod/ds_demo.c hello_mod/hello.c
./vm ssh "cat ~/hello_mod/hello.c" | diff - hello_mod/hello.c
./vm ssh "cat ~/ds_demo_mod/ds_demo.c" | diff - ds_demo_mod/ds_demo.c
```

Result: the host copies were newer. The differences were formatting in
`ds_demo.c` and the author name in `hello.c`.

```
./vm sync ds_demo_mod
./vm sync hello_mod
./vm ssh 'cd ~/hello_mod && make'
```

Result: `make` rebuilt nothing for `hello`, although `hello.c` had changed.
The sync had copied the host's older timestamp, so `hello.o` looked newer than
its source. `./vm sync` was changed from `rsync -az` to `rsync -rlpcz`, which
compares content and does not copy timestamps, and retested with a file given
a deliberately old timestamp.

The full sequence, after the fix:

```
./vm ssh 'cd ~/hello_mod && make; sudo insmod hello.ko; sudo rmmod hello; sudo dmesg | tail'
./vm ssh 'cd ~/ds_demo_mod && make;
          sudo insmod ds_demo.ko blink_ms=500; sleep 1.2;
          cat /sys/module/ds_demo/parameters/blink_ms;
          echo 200 | sudo tee /sys/module/ds_demo/parameters/blink_ms; sleep 1;
          echo 50 | sudo tee /sys/module/ds_demo/parameters/blink_ms;
          cat /sys/module/ds_demo/parameters/blink_ms;
          sudo rmmod ds_demo; sudo dmesg | tail -12'
```

Result: both modules built, loaded and unloaded. The parameter read back as
`500`, then `200`; the write of `50` failed with "Invalid argument" and the
value stayed `200`. The kernel log from this run is the one quoted in
`README.md`.
