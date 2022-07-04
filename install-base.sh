#!/bin/bash

dnf install https://dl.Fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm -y
dnf update -y

yum install -y gcc gcc-c++ \
    gcc-toolset-10-gcc gcc-toolset-10-gcc-c++ gcc-toolset-10-binutils \
    python3 python3-devel libicu libicu-devel zlib zlib-devel bzip2 bzip2-devel \
    make cmake automake \
    pigz zstd libzstd-devel \
    openssl-devel ncurses-devel libtirpc-devel libudev-devel 

