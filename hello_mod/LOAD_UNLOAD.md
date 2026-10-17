## Build, load, test, unload

```
make                       # produces hello.ko
modinfo hello.ko           # inspect metadata
sudo insmod hello.ko       # load it
sudo dmesg | tail          # see "hello: module loaded"
lsmod | grep hello         # confirm it's loaded
sudo rmmod hello           # unload it
sudo dmesg | tail          # see "hello: module unloaded"
```
