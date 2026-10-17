## Build, load, test, unload

```
make                                   # produces ds_demo.ko
modinfo ds_demo.ko                     # inspect metadata, including the blink_ms parameter
sudo insmod ds_demo.ko blink_ms=500    # load it with a 500 ms tick (default is 1000)
lsmod | grep ds_demo                   # confirm it's loaded
sudo dmesg | tail                      # see "ds_demo: loaded" and the ticks

cat /sys/module/ds_demo/parameters/blink_ms                    # read the parameter
echo 200 | sudo tee /sys/module/ds_demo/parameters/blink_ms    # change it while loaded
echo 50  | sudo tee /sys/module/ds_demo/parameters/blink_ms    # rejected: outside 100..10000

sudo rmmod ds_demo                     # unload it
sudo dmesg | tail                      # see "ds_demo: unloaded after N ticks"
```
