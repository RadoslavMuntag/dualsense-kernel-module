# Linux kernel module and hardware interaction

Author: Radoslav Muntag

Two small kernel modules and an investigation of a USB peripheral, the Sony
DualSense (PS5) controller.

| Path | What it is |
|---|---|
| `hello_mod/` | Minimal module: logs on load and unload |
| `ds_demo_mod/` | Module with a configurable parameter, `blink_ms`, that drives an LED trigger |
| `NOTES.md` | Terminal output from my own runs |
| `VM.md`, `vm` | The development VM and the script that drives it |
| `COMMANDS.md` | Log of the commands used to set up and investigate |

## Environment

| | |
|---|---|
| OS | Ubuntu 24.04.5 LTS, in a QEMU/KVM virtual machine |
| Kernel | `6.8.0-146-generic` |
| Compiler | gcc 13.3.0 |
| Host | Arch-based Linux; the controller is passed through to the VM over USB |

I used a VM so that a faulty module could only crash the guest.

## Build, load, test, unload

Run inside the VM, in `ds_demo_mod/`:

```
make
sudo insmod ds_demo.ko blink_ms=500
sudo dmesg | tail
cat /sys/module/ds_demo/parameters/blink_ms
echo 200 | sudo tee /sys/module/ds_demo/parameters/blink_ms
sudo rmmod ds_demo
sudo dmesg | tail
```

To make an LED follow the module while it is loaded:

```
ls /sys/class/leds/                                        # pick an LED
echo ds-demo | sudo tee /sys/class/leds/<led>/trigger      # attach it
cat /sys/class/leds/<led>/brightness                       # changes every blink_ms
```

`hello_mod/` is the same without the parameter: `make`,
`sudo insmod hello.ko`, `sudo rmmod hello`.

## The module parameter

`ds_demo` runs a "tick" every `blink_ms` milliseconds. Each tick is logged and
toggles an LED trigger (see the next section).

- `blink_ms` can be set at load time and changed while loaded through
  `/sys/module/ds_demo/parameters/blink_ms`.
- Values outside 100..10000 are rejected with `-EINVAL`.
- The tick runs from a delayed work item, which is cancelled with
  `cancel_delayed_work_sync()` on unload.

Kernel log from the commands above, plus a rejected write of `50`:

```
[18749.911343] ds_demo: blink_ms set to 500
[18749.911415] ds_demo: loaded, blink_ms=500
[18750.416853] ds_demo: tick 1: LED would be ON (simulated, blink_ms=500)
[18750.920844] ds_demo: tick 2: LED would be OFF (simulated, blink_ms=500)
[18751.121959] ds_demo: blink_ms set to 200
[18751.424809] ds_demo: tick 3: LED would be ON (simulated, blink_ms=200)
[18751.632836] ds_demo: tick 4: LED would be OFF (simulated, blink_ms=200)
[18751.840850] ds_demo: tick 5: LED would be ON (simulated, blink_ms=200)
[18752.048787] ds_demo: tick 6: LED would be OFF (simulated, blink_ms=200)
[18752.131513] ds_demo: blink_ms=50 rejected, allowed range 100..10000
[18752.142638] ds_demo: unloaded after 6 ticks
```

And for `hello`:

```
[18748.740851] hello: module loaded
[18748.752300] hello: module unloaded
```

## The LED trigger

From version 0.3, `ds_demo` registers an LED trigger named `ds-demo` with the
kernel's LED subsystem. On every tick it calls `led_trigger_event()` with
`LED_FULL` or `LED_OFF`. Any LED on the system can be attached to the trigger
through its `trigger` file in sysfs, and it then blinks at `blink_ms`.

**What is real and what is not:**

| Part | Provided by | Status |
|---|---|---|
| Timing, parameter, on/off decision | `ds_demo` (my module) | Real kernel code |
| Passing the event to attached LEDs | Kernel LED subsystem | Real kernel code |
| Switching the LED itself | The driver that owns that LED | Depends on the LED |

`ds_demo` never sends data to a device. It is not a driver for the controller.
The hardware access is done by whichever driver registered the LED: for the
DualSense that is `hid_playstation`, which turns a brightness change into a HID
output report.

**Tested on an emulated LED.** In the VM I attached the trigger to
`input1::scrolllock`, the scroll-lock LED of QEMU's emulated keyboard. That is
not physical hardware. With `blink_ms=300`, reading its brightness every 300 ms
gave:

```
$ echo ds-demo | sudo tee /sys/class/leds/input1::scrolllock/trigger
$ cat /sys/class/leds/input1::scrolllock/brightness     (six reads)
0 1 0 1 0 1
```

After `rmmod`, the LED was off and its trigger had returned to `[none]`.

**On the DualSense:** <!-- TODO: record your result here. --> not yet
verified. With `hid_playstation` loaded and the controller attached, its LEDs
should appear in `/sys/class/leds/` (player LEDs and the lightbar), and
attaching one to `ds-demo` should blink the physical LED.

The log line still reads "LED would be ON (simulated…)". That wording dates
from before the trigger existed; the event is now really sent.

## The device: Sony DualSense controller

**How Linux identifies it.** Over USB the controller reports vendor ID `054c`
(Sony) and product ID `0ce6`. The kernel logs this when it is plugged in:

```
usb 1-1: New USB device found, idVendor=054c, idProduct=0ce6, bcdDevice= 1.00
usb 1-1: Product: DualSense Wireless Controller
usb 1-1: Manufacturer: Sony Interactive Entertainment
```

It exposes four interfaces. Linux picks a driver for each interface, not for
the device as a whole, by matching the interface class and the IDs against the
tables that drivers register.

| Interface | Class | Purpose |
|---|---|---|
| 0, 1, 2 | `01` audio | Speaker, microphone and headset jack |
| 3 | `03` HID | Buttons, sticks, sensors, LEDs, rumble |

**Which driver handles it.** This differs between my two systems:

| | Audio interfaces | HID interface |
|---|---|---|
| Host | `snd-usb-audio` | `usbhid`, then `hid_playstation` |
| VM, as installed | none | `usbhid`, then `hid-generic` |

`usbhid` carries HID data over USB. On top of it, a HID driver interprets the
data. Sony's `hid_playstation` lists `054c:0ce6` in its ID table, so it wins
on the host. A fresh Ubuntu cloud image does not include that module (it ships
in `linux-modules-extra`), so the fallback `hid-generic` took the device. I
later installed `linux-modules-extra-6.8.0-146-generic` in the VM to get
`hid_playstation` and the controller's LEDs there too.

**How I could communicate with it.**

- *Raw HID reports* through `/dev/hidraw0`. The controller continuously sends
  64-byte input reports that start with report ID `01`. Reading the node in
  the VM with `sudo hexdump -C /dev/hidraw0` shows them.
- *Output reports* written to the same node control the lightbar, player LEDs
  and rumble.
- *From a kernel module*, by registering a `hid_driver` with `054c:0ce6` in its
  ID table. Its `raw_event` callback would receive the input reports, and
  `hid_hw_output_report()` would send output reports.
- *Through the existing driver*: `hid_playstation` exposes the LEDs under
  `/sys/class/leds/`, the battery as a power supply, and the buttons and
  sticks as input devices. This is the route `ds_demo` uses, by way of its LED
  trigger.

## What works

- Both modules build for the running kernel, load, log and unload cleanly.
- `blink_ms` works at load time and at runtime, with range checking.
- The controller is passed through to the VM and delivers live data.
- The `ds-demo` LED trigger registers, blinks an attached LED at `blink_ms`,
  and is removed cleanly on unload. Tested on the VM's emulated keyboard LED.

**Not done:** `ds_demo` does not talk to the controller directly. Blinking a
DualSense LED goes through `hid_playstation`, and that step is not yet
verified on the physical controller.

## Problems encountered

- **The kernel changed under me.** Ubuntu's automatic updater installed kernel
  `6.8.0-146` while `6.8.0-142` was running, and the VM switched at its next
  boot. `hello.ko`, built for `142`, would no longer load. `modinfo` shows the
  kernel a module was built for in its `vermagic` field. I rebuilt the module
  and held the kernel packages with `apt-mark hold`.
- **clangd flagged every line.** My editor runs on the host, which had no
  Ubuntu kernel headers. Copying the headers out of the VM and giving clangd
  the real compiler flags fixed it.
- **Two build messages that look like errors but are not.** "The compiler
  differs from the one used to build the kernel" compares compiler names as
  text (`x86_64-linux-gnu-gcc-13` against `gcc-13`); the version is identical.
  "Skipping BTF generation" means optional debug type information was left
  out because the headers package has no `vmlinux`.
- **Loading taints the kernel.** The log says "module verification failed"
  because my module is unsigned and out of tree. It still loads.

## What I learned

<!-- TODO: rewrite this section in your own words before submitting. -->

- A module is tied to one exact kernel build, and it must be rebuilt when the
  kernel changes.
- Code that runs later, such as a timer or work item, has to be stopped before
  the module is unloaded, or the kernel would jump into freed memory.
- A parameter can be written from userspace at any moment, so the module must
  validate it and read it safely.
- Linux binds drivers per USB interface, and the HID layer sits on top of the
  USB transport as a separate bus with its own drivers.

## References

<!-- TODO: keep only what you actually used, and add anything missing. -->

- Linux kernel documentation: building external modules
  (`Documentation/kbuild/modules.rst`)
- Linux kernel documentation: workqueues (`Documentation/core-api/workqueue.rst`)
- Linux kernel source: `drivers/hid/hid-playstation.c`, `include/linux/moduleparam.h`,
  `include/linux/leds.h`
- Linux kernel documentation: LED class and triggers (`Documentation/leds/leds-class.rst`)
- Ubuntu cloud images: https://cloud-images.ubuntu.com/noble/current/

## AI assistance

I used Claude Code (Anthropic's Claude) during this work.

- **Written by the AI:** the `vm` script, `VM.md`, `COMMANDS.md`, the `.clangd`
  configuration, the udev rule, and the first draft of this README.
- **Investigated with the AI:** the USB and driver findings for the controller,
  the timeline of the kernel upgrade, and the clangd and sync problems.
- **Module source:** 
  - Hello example and research on modules - Johannes 4GNU_Linux - Let's code a Linux Driver course (https://www.youtube.com/playlist?list=PLCGpd0Do5-I3b5TtyqeF1UdyD4C-S-dMa)
  - pr_info/pr_err and pr_fmt - (https://docs.kernel.org/core-api/printk-basics.html)
  - kstrtoint - https://docs.kernel.org/core-api/kernel-api.html



How I verified the results: every command in this README was run in the VM,
and the log output above comes from those runs. `COMMANDS.md` records the
commands behind the investigation, with their results.
