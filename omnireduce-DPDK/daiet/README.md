# DAIET
### Install dependencies
- Dependencies: 
`make, coreutils, gcc, libc headers, kernel headers, libnuma-dev, Python version 2.7+ or 3.2+, kmod, pciutils, build-essential, boost-program-options, cmake, libhiredis-dev`

In Debian/Ubuntu:
```sh
apt install -f make \
coreutils \
gcc \
libc6-dev \
linux-headers-$(uname -r) \
libnuma-dev \
python \
kmod \
pciutils \
build-essential \
libboost-program-options-dev \
libboost-all-dev \
cmake \
libhiredis-dev
```

In Fedora:
```sh
dnf install make \
automake \
coreutils \
gcc \
gcc-c++ \
glibc-devel \
kernel-devel \
kernel-headers \
numactl-devel \
python \
kmod \
pciutils \
boost-program-options
```

### DPDK Setup
- See [here](https://doc.dpdk.org/guides/linux_gsg/sys_reqs.html)

### Compile DPDK
- Run:
```sh
cd lib/dpdk
make defconfig T=x86_64-native-linuxapp-gcc
make -j
sudo make install
cd ../..
```

### Bind the interfaces
- Set the name of the interface to bind with DPDK in `dpdk-config.sh`
- Run: `. ./dpdk-config.sh`

### Compile the DAIET library
- Run:
```sh
make -j
sudo make libinstall
```

### Configuration
- Configuration parameters (e.g., number of workers, IPs and ports) in `daiet.cfg`

## Utils

- Get the hugepage size:
```sh
awk '/Hugepagesize/ {print $2}' /proc/meminfo
```

- Get the total huge page numbers:
```sh
awk '/HugePages_Total/ {print $2} ' /proc/meminfo
```

- Unmount the hugepages:
```sh
umount `awk '/hugetlbfs/ {print $2}' /proc/mounts`
```

- Mount hugepage folder:
```sh
mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```

- Check the CPU layout using using the DPDK cpu\_layout utility:
```sh
cd lib/dpdk
usertools/cpu_layout.py
```

- Check your NIC id and related socket id:
```sh
# List all the NICs with PCI address and device IDs.
lspci -nn | grep Eth
```
- Check the PCI device related numa node id:
```sh
cat /sys/bus/pci/devices/0000\:xx\:00.x/numa_node
```
Usually 0x:00.x is on socket 0 and 8x:00.x is on socket 1. 
Note: To get the best performance, ensure that the core and NICs are in the same socket. In the example above 85:00.0 is on socket 1 and should be used by cores on socket 1 for the best performance.

> Note:
> to support C++ applications, DPDK is patched with:
> ```sh
> cd lib/dpdk
> patch -p1 < cpp_support.patch
> ```
