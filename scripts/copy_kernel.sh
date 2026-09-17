#!/bin/bash

K=~/rpi5-kernel/bazel-bin/common/rpi5
D=~/aosp-rpi5/device/brcm/rpi5-kernel

# Kernel-Image 
cp $K/Image $D/

# Device Trees
cp $K/arch/arm64/boot/dts/broadcom/bcm2712*.dtb $D/

# Overlays
cp $K/arch/arm64/boot/dts/overlays/*.dtbo $D/overlays/

# Kernelmodule: im Build verschachtelt, im Device-Tree aber flach
find $K -name '*.ko' -exec cp {} $D/modules/ \;
