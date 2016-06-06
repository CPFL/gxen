# gxen

The research prototype of GPUvm.

## Prerequisites

We tested & evaluated on the following environments.

+ gxen stable branch OR latest
    + stable: https://github.com/CS005/gxen/tree/stable
    + latest: https://github.com/CS005/gxen/tree/master
+ Linux 3.6.5 kernel
    + PV version & FV version is available
    + https://github.com/Constellation/linux-3.6.5
    + `gdev-latest` is the branch that already includes the [gdev module side patches](https://github.com/CPFL/gdev/blob/605e69e70ce7b4c505be91696612e98649ec383f/mod/linux/patches/gdev-nouveau-3.6.patch).
+ NVIDIA Quadro6000 GPU
    + We tested with NVIDIA Quadro6000 nvc0 GPU
+ Kernel mode gdev
    + `605e69e70ce7b4c505be91696612e98649ec383f` is tested (In this version, cubin should be built with NVCC 4.2. This is caused by this kernel mode gdev's limitation)
    + `054b48615bb599d9542d0d6552a7c72bdb62c60c` is tested
      + But reverting `d4a6697583ca3d5606711402be121da0bf9875e2` is recommended since it acquires a lot of memory (since we only support BAR remapping for BAR3 (not BAR1: it is only used for submitting a command request on the NVIDIA driver. Submission requests are trapped by gxen) in this prototype, initializing this memory area takes a lot of time)
+ Rodinia benchmarks
    + https://github.com/shinpei0208/gdev-bench

## Build gxen

### Build Xen part

stubdom cannot be built since we use C++ in qemu-dm nvc0 device model.
So in order to build it, try
```
make xen
make tools
make install-xen
make install-tools
```

And boost, g++, libpciaccess libraries are necessary.

### Build A3 (GPU Access Aggregator) part

A3 is located on `tools/a3`. To build it (stable branch), try
```
make -C tools/a3/build
```

To build it (in latest branch), try
```
cd tools/a3
cmake -H. -Bout
make -C out
```

Then you get a3 binary on `tools/a3/build/a3` OR `tools/a3/out/a3`. You can see options by providing `--help` to `a3`.

```
usage: a3 [options] [program_file] [arguments]
options:
  -h, --help              print this message
  -v, --version           print the version
  -t, --through           through I/O
      --lazy-shadowing    Enable lazy shadowing
      --bar3-remapping    Enable BAR3 remapping
```

### Build gdev

To follow the old gdev kernel module build instruction. Generate gdev.ko from that.

### Build Linux kernel

Get Linux kernel 3.6.5 from the above tree and build it.

## Use gxen

### Initialize GPU

Initialization part depends on the host nouveau driver. So you need to load the 3.6.5 nouveau on host environment.
At this time, you need to specify the following kernel command line to Host Linux on Xen,

```
nouveau.noaccel=0 nouveau.modeset=2
```

And you don't use X-server since it may load nouvea & use it. You need to load host nouveau driver, but it is used for
initializing GPU and you must not touch GPU from the host.

And don't enable Xen iommu support, gxen doesn't use VT-d.

### Writing nvc0=0

To expose virtualized GPU, writing `nvc0=0` to your Xen's hvm file.

### Execute A3

Need to execute `a3` with an appropriate GPU device bdf number.

### Boot Xen HVM with above Linux 3.6.5 kernel

Boot Xen HVM with a virtualized GPU.

In this time, you need to specify the following command to VM's Linux commandline to make gdev works,
```
nouveau.noaccel=0 nouveau.modeset=2
```

Before booting HVM, you need to run `a3` command with bus number. such as `build/a3 --bar3-remapping --lazy-shadowing 0300`

Recommend not to load X-server on HVM.

### Load gdev module on HVM

And then, you need to load gdev.ko. Follow the gdev kernel module instructions.

### Run GPU benchmarks

You can run GPU benchmarks with gxen. For example, `gdev/test/cuda/user/madd`.

## Multiplexing

You can load multiple VMs that use virtualized GPU.
But the resources (such as channels, device memory) are splitted statically, so you need to adjust `tools/a3/a3\_config.h`'s configuration.
And if you change this configuration, remember to build Xen tools by executing `make tools` and `make install-tools`.
