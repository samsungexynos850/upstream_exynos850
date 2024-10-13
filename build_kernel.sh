#!/bin/bash

export PATH=/home/thomas/toolchains/A217M/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/:/home/thomas/toolchains/A217M/clang/host/linux-x86/clang-r353983c/bin/:$PATH

export PLATFORM_VERSION=12
export ANDROID_MAJOR_VERSION=s
export ARCH=arm64

mkdir out
make ARCH=arm64 O=out CROSS_COMPILE=aarch64-linux-androidkernel- CC=clang CLANG_TRIPLE=aarch64-linux-gnu- exynos850-a21snsxx_defconfig
make ARCH=arm64 O=out CROSS_COMPILE=aarch64-linux-androidkernel- CC=clang CLANG_TRIPLE=aarch64-linux-gnu- -j64
