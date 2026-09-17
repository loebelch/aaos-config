# AAOS configuration

This repository contains Android Automotive OS configuration for running
linuxptp gPTP and AVTP on a Raspberry Pi 5 target.

## Contents

- `linuxptp/Android.bp.linuxptp`: Soong definitions for the linuxptp static
  library and binaries (`ptp4l`, `phc2sys`, `pmc`, `phc_ctl`, and
  `hwstamp_ctl`).
- `libavtp/Android.bp.libavtp`: Soong definition for the vendor-available
  shared `libavtp` library.
- `linuxptp/gptp.cfg`: IEEE 802.1AS automotive gPTP configuration for `eth0`.
- `linuxptp/gptp.rc`: Android init services and the
  `vendor.gptp.enable` property trigger.
- `scripts/copy_kernel.sh`: Copies the Raspberry Pi 5 kernel image, device
  trees, overlays, and kernel modules into the AOSP device tree.

## linuxptp integration

Place the linuxptp source tree in the Android source tree at
`external/linuxptp`. The Soong file is intended to be used as
`external/linuxptp/Android.bp`; it expects the upstream source files and
`COPYING` to be present in that directory.

The resulting binaries are vendor binaries. The init configuration expects:

- `/vendor/bin/ptp4l`
- `/vendor/bin/phc2sys`
- `/vendor/etc/gptp.cfg`
- `/dev/ptp0`
- an Ethernet interface named `eth0`

Build the linuxptp targets from the AOSP root with:

```sh
m ptp4l phc2sys pmc phc_ctl hwstamp_ctl
```

Install `gptp.cfg` in `/vendor/etc/` and include `gptp.rc` in the `ptp4l`
module installation. The services run in the `gptp` SELinux domain and require
the corresponding device policy to be present in the target product.

## libavtp integration

Place the libavtp source tree in the Android source tree at
`external/libavtp`. The Soong file is intended to be used as
`external/libavtp/Android.bp`; it expects the upstream source files, public
headers, and `LICENSE` to be present in that directory.

Build the AVTP library from the AOSP root with:

```sh
m libavtp
```

The library is built as a shared vendor-available module and exports headers
from the source tree's `include` directory.

## Runtime control

The gPTP service is disabled by default. Enable it with:

```sh
adb shell setprop vendor.gptp.enable 1
```

Stop it with:

```sh
adb shell setprop vendor.gptp.enable 0
```

The enable trigger starts `ptp4l`. `phc2sys` is defined as a separate
disabled service and is not started by the current enable trigger.

## Kernel copy helper

Before running `scripts/copy_kernel.sh`, set the paths in the script to match
the local kernel build output and AOSP device directory. By default it expects:

- Kernel build output: `~/rpi5-kernel/bazel-bin/common/rpi5`
- AOSP device directory: `~/aosp-rpi5/device/brcm/rpi5-kernel`

Run it from any directory with:

```sh
./scripts/copy_kernel.sh
```

The script copies the kernel image, Broadcom device trees, overlays, and all
`.ko` modules into the target device directory.