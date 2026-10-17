# Ubuntu VM for kernel module development

This documents the development environment. The assignment itself is described
in [README.md](README.md).

This directory holds a small Ubuntu 24.04 LTS virtual machine used to build and
test a Linux kernel module. Everything is driven by one Bash script, `vm`.

## Why a VM at all

A kernel module runs inside the kernel, so a bug in it can hang or crash the
whole machine. The host here runs Arch-based Omarchy, not Ubuntu, and the task
asks for Ubuntu. A VM solves both problems: the module runs against a real
Ubuntu kernel, and a crash only takes down the guest.

A Docker container would not work. Containers share the host kernel, so a
module loaded from a container would be loaded into the host's Arch kernel.

## What the setup consists of

| Piece | Choice | Reason |
|---|---|---|
| Hypervisor | QEMU with KVM acceleration | The CPU supports virtualization and `/dev/kvm` is usable without root, so the guest runs at near-native speed |
| Guest OS | Ubuntu 24.04 LTS cloud image | Official prebuilt disk image, so there is no installer to click through |
| First-boot config | cloud-init | Creates the user, installs the SSH key and packages automatically |
| Access | SSH on `127.0.0.1:2222` | No graphical window needed; the port is only reachable from this machine |

## Initial setup, step by step

These are the steps that were carried out, in order.

1. **Checked the host.** The CPU exposes virtualization extensions, `/dev/kvm`
   exists, and there were 12 cores and about 10 GB of free memory. No
   hypervisor was installed.

2. **Installed QEMU.** This is the only step that needs root:

   ```
   sudo pacman -S qemu-base
   ```

   `qemu-base` is the headless build. It provides `qemu-system-x86_64` (the
   emulator) and `qemu-img` (the disk image tool).

3. **Downloaded the Ubuntu cloud image** and verified it against Ubuntu's
   published checksums:

   ```
   curl -fLO https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
   curl -fLO https://cloud-images.ubuntu.com/noble/current/SHA256SUMS
   grep noble-server-cloudimg-amd64.img SHA256SUMS | sha256sum -c -
   ```

4. **Wrote the `vm` script** (explained below).

5. **Booted the VM for the first time** with `./vm start`. On this first boot
   the script created the disk and the cloud-init seed, and cloud-init inside
   the guest installed the build tools.

6. **Verified the result** inside the guest:

   | Check | Result |
   |---|---|
   | OS | Ubuntu 24.04.5 LTS |
   | Kernel | 6.8.0-142-generic |
   | Kernel headers | `/lib/modules/6.8.0-142-generic/build` present |

   These are the first-boot values. Ubuntu's automatic updater later moved the
   VM to kernel `6.8.0-146-generic`; see "Kernel upgrades" below.
   | Compiler | gcc 13.3.0, GNU Make 4.3 |
   | Resources | 4 CPUs, 3.8 GiB RAM, 29 GB disk |

## Files in this directory

| File | What it is | Created by |
|---|---|---|
| `vm` | The control script | Written by hand |
| `70-dualsense-vm.rules` | udev rule for USB passthrough | Written by hand |
| `noble-server-cloudimg-amd64.img` | Pristine Ubuntu image, never modified | Downloaded |
| `SHA256SUMS` | Ubuntu's checksums for the image | Downloaded |
| `disk.qcow2` | The VM's actual disk | `./vm start`, first run |
| `seed.iso` | cloud-init configuration | `./vm start`, first run |
| `known_hosts` | The VM's SSH host key | First SSH connection |
| `console.log` | Guest serial console output | Each boot |
| `qemu.pid` | Process ID of the running VM | Each boot |

## Using the script

```
./vm start     boot the VM and wait until SSH answers
./vm ssh       open a shell in the VM
./vm ssh CMD   run one command, e.g. ./vm ssh uname -r
./vm log       follow the guest serial console
./vm status    show whether the VM is running
./vm stop      shut the VM down
./vm reset     delete the VM disk and start again from clean Ubuntu

./vm sync DIR [NAME]   mirror a host directory to ~/NAME in the VM
./vm headers           copy the guest's kernel headers to kernel-headers/

./vm snapshot list            show saved snapshots
./vm snapshot create [name]   save the current disk state
./vm snapshot revert <name>   go back to a snapshot
./vm snapshot delete <name>   remove a snapshot
```

The guest user is `ubuntu`, with passwordless `sudo`. SSH logs in with the
host's `~/.ssh/id_ed25519` key. Password login over SSH is disabled.

To copy files in or out:

```
scp -P 2222 -o UserKnownHostsFile=./known_hosts hello.c ubuntu@127.0.0.1:
```

Defaults can be overridden with environment variables:

| Variable | Default | Applies |
|---|---|---|
| `VM_CPUS` | `4` | Every start |
| `VM_MEM` | `4G` | Every start |
| `VM_SSH_PORT` | `2222` | Every start |
| `VM_USB` | `054c:0ce6` | Every start |
| `VM_DISK_SIZE` | `30G` | Only when the disk is created |
| `VM_SSH_KEY` | `~/.ssh/id_ed25519` | Only when the seed is created |

## How the script works

### Creating the disk

```
qemu-img create -f qcow2 -b noble-server-cloudimg-amd64.img -F qcow2 disk.qcow2 30G
```

`disk.qcow2` is a copy-on-write overlay. It starts almost empty and records
only the blocks that differ from the downloaded image, which stays untouched.
This is what makes `./vm reset` cheap: deleting the overlay returns the VM to
clean Ubuntu without downloading anything again. The `30G` is a maximum size,
not space used up front. Ubuntu grows its root filesystem to fill it on first
boot.

### Configuring Ubuntu with cloud-init

A cloud image has no user and no password. On first boot, cloud-init looks for
a disk labelled `CIDATA` and reads two files from it:

- `meta-data` sets the instance ID and the hostname (`kmod-dev`).
- `user-data` describes what to configure.

The script generates both and packs them into `seed.iso` with `bsdtar`, which
was already on the host and can write ISO images with a volume label. The
`user-data` does the following:

- Creates the `ubuntu` user with passwordless `sudo` and the host's SSH public
  key.
- Sets the password `ubuntu` for that user. It is not used over SSH, where
  password login is off.
- Installs `build-essential`, `git` and `kmod`.
- Runs `apt-get install linux-headers-$(uname -r)`.

The last line matters most. A module must be built against headers for the
exact kernel that is running. Installing by `uname -r` guarantees a match,
where a generic headers package could pull in headers for a newer kernel than
the one booted.

cloud-init applies this configuration once per instance ID. Later boots skip
it.

### Starting QEMU

```
qemu-system-x86_64 \
  -machine q35,accel=kvm -cpu host -smp 4 -m 4G \
  -drive file=disk.qcow2,if=virtio,format=qcow2 \
  -drive file=seed.iso,if=virtio,format=raw,readonly=on \
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2222-:22 \
  -display none -serial file:console.log \
  -daemonize -pidfile qemu.pid
```

| Option | Meaning |
|---|---|
| `-machine q35,accel=kvm` | Emulate a modern PC chipset and use KVM for hardware acceleration |
| `-cpu host` | Expose the host CPU's features to the guest |
| `-smp 4 -m 4G` | 4 virtual CPUs, 4 GB of memory |
| `-drive ... if=virtio` | Attach the disk and the seed as fast paravirtual block devices |
| `-nic user,...` | User-mode networking: the guest reaches the internet through the host, with no root or bridge setup |
| `hostfwd=tcp:127.0.0.1:2222-:22` | Forward host port 2222 to the guest's SSH port, bound to localhost only |
| `-display none` | No graphical window |
| `-serial file:console.log` | Write the guest's serial console to a file |
| `-daemonize -pidfile qemu.pid` | Run in the background and record the process ID |

After launching QEMU, `start` tries an SSH connection every two seconds, for up
to three minutes, and returns once one succeeds.

### The other commands

- **`ssh`** connects with a `known_hosts` file kept in this directory. The VM
  gets a new host key on every reset, and keeping it out of
  `~/.ssh/known_hosts` avoids host-key warnings there.
- **`stop`** runs `sudo poweroff` in the guest and waits up to 30 seconds. If
  the QEMU process is still alive after that, it is killed. That fallback is
  for the case where a module has hung the guest kernel and SSH no longer
  answers.
- **`status`** checks whether the process recorded in `qemu.pid` is alive.
- **`log`** follows `console.log`.
- **`reset`** stops the VM and deletes the disk, seed, host key and console
  log. The next `start` builds a new VM.

### Snapshots

A snapshot records the state of the VM's disk at one moment, so a later
experiment can be undone. A typical use is to snapshot a working setup before
loading a risky module:

```
./vm snapshot create before-test
# ... the module corrupts something or the guest stops booting ...
./vm snapshot revert before-test
```

The script uses qcow2 internal snapshots, managed with `qemu-img snapshot`
(`-c` create, `-l` list, `-a` apply, `-d` delete). They are stored inside
`disk.qcow2`, so there are no extra files.

- **Disk only.** A snapshot holds the disk, not running memory. After a revert
  the VM boots fresh from the saved disk, and loaded modules are gone.
- **The VM restarts.** `qemu-img` cannot change an image that QEMU has open. If
  the VM is running, `create` and `delete` shut it down cleanly, do the work
  and boot it again. `revert` kills it instead of shutting down, because the
  current disk state is being discarded anyway. This also means `revert` works
  when the guest kernel is hung.
- **Revert is destructive.** Everything written since the snapshot is lost.
- **Names.** Without a name, `create` uses a timestamp such as
  `20261010-152328`. Names may contain letters, digits, `.`, `_` and `-`.
- **`reset` deletes snapshots too**, since it deletes `disk.qcow2`.

### Syncing source from the host

The module source is kept and edited on the host, then copied into the guest
to be built:

```
./vm sync ~/Work/my-module
./vm ssh 'cd my-module && make'
```

`sync` mirrors the directory to `~/<name>` in the guest, where the name
defaults to the directory's own name. It runs `rsync` over the same SSH
connection settings as `./vm ssh`:

```
rsync -rlpcz --delete --itemize-changes --exclude=... -e "ssh -p 2222 ..." DIR/ ubuntu@127.0.0.1:NAME/
```

- **It is a one-way mirror.** `--delete` removes files in the guest copy that
  no longer exist on the host, so a renamed source file does not linger and
  get compiled. Edits made in the guest copy are overwritten.
- **Build products survive.** `*.o`, `*.ko`, `*.mod`, `*.mod.c`, `.*.cmd`,
  `Module.symvers` and `modules.order` are excluded, so they are neither
  copied nor deleted, and `make` in the guest only rebuilds what changed.
  `.git/` is excluded too.
- **Changed files get a fresh timestamp.** Files are compared by content
  (`-c`) and host timestamps are not copied (no `-t`). A changed file therefore
  always looks newer than its object file, so `make` rebuilds it. An earlier
  version copied timestamps, and `make` then skipped a file that had been
  edited on the host before the last guest build.
- **Changes are listed.** Each transferred or deleted file is printed.

### Editor tooling on the host

clangd runs on the host, which has neither Ubuntu's kernel headers nor the
compiler flags a module needs, so at first it reported every line as an error.
Two pieces fix that:

- `./vm headers` copies the running guest kernel's headers to
  `kernel-headers/`, with a `current` link to the running version.
- `.clangd` points clangd at that copy, using the include paths and defines
  from the real build command (taken from `.hello.o.cmd` in the guest), minus
  the gcc-only options.

The same copy provides the kernel style checker:

```
perl kernel-headers/current/scripts/checkpatch.pl --no-tree -f ds_demo_mod/ds_demo.c
```

### Kernel upgrades

Ubuntu cloud images run `unattended-upgrades`, which installs security updates
without asking. On 2026-10-10 it installed kernel `6.8.0-146` and matching
headers while `6.8.0-142` was running, and the VM switched at its next boot.
A module built before the switch no longer loads and must be rebuilt.

To stop this happening mid-work, the kernel packages are now held:

```
sudo apt-mark hold linux-virtual linux-image-virtual linux-headers-virtual linux-headers-generic
```

After any kernel change, run `./vm headers` again and rebuild the modules.

### USB passthrough

The guest only sees USB devices attached to a virtual USB controller, so
`start` adds one (`-device qemu-xhci`, a USB 3 xHCI controller). For each ID in
`VM_USB` it also adds:

```
-device usb-host,vendorid=0x054c,productid=0x0ce6
```

This hands the real host device to the guest. The default ID, `054c:0ce6`, is
the Sony DualSense controller. While the VM holds the device, the host's own
drivers are detached from it. If the device is not plugged in at boot, it
attaches to the VM when it is connected later.

Passthrough needs two one-time host steps, both requiring root:

```
sudo pacman -S qemu-hw-usb-host
sudo install -m 644 70-dualsense-vm.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --attr-match=idVendor=054c
```

- `qemu-hw-usb-host` is the QEMU module that implements `usb-host`. It is not
  part of `qemu-base`.
- The udev rule gives the logged-in user access to the controller's node under
  `/dev/bus/usb/`, which is otherwise writable only by root. QEMU runs as that
  user and must open the node.

If either step is missing, `start` prints a warning and boots the VM without
the device. Set `VM_USB=""` to pass nothing through.

The guest's kernel package does not include Sony's `hid_playstation` driver
(it ships in `linux-modules-extra`), so in the guest the controller is handled
by the generic HID driver unless a custom module claims it.

## Things worth knowing while developing the module

- **First boot is slower.** `start` returns as soon as SSH answers, but
  cloud-init keeps installing packages for a minute or two. Run
  `./vm ssh cloud-init status --wait` to block until it is finished.
- **Watch the serial console.** Ordinary `printk` output is read with
  `sudo dmesg` in the guest. If the kernel panics, SSH dies with it, and the
  panic message is only visible through `./vm log`.
- **Keep the source on the host.** There is no shared folder; use `./vm sync`
  to copy it in. `./vm reset` and `./vm snapshot revert` discard what is in
  the guest, so anything edited only there can be lost.
