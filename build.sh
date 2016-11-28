#! /bin/bash

. ./list.sh

# Logic Code

echo "Checking whether the sources tarball is unique..."
if [ "$(echo linux-*.tar.* | wc -w)" != "1" ]; then
	echo "More than one linux kernel sources. Exit."
fi
if [ "$(echo u-boot-*.tar.* | wc -w)" != "1" ]; then
	echo "More than one u-boot sources. Exit."
fi
LINUX_SRC="$(echo linux-*.tar.*)"
UBOOT_SRC="$(echo u-boot-*.tar.*)"

LINUX_DIR="$(echo $LINUX_SRC | sed 's/\.tar\..*//g')"
UBOOT_DIR="$(echo $UBOOT_SRC | sed 's/\.tar\..*//g')"

echo "Building u-boot..."

OUT_DIR="${PWD}/out"
mkdir -p "$OUT_DIR"

LOG_DIR="${PWD}/log"
mkdir -p "$LOG_DIR"
[ "$BUILD_UBOOT" != "0" ] &&
for i in $UBOOT_TARGETS
do
	UBOOT_CNAME="$(echo $i | cut -d = -f 1)"
	UBOOT_AOSCNAME="$(echo $i | cut -d = -f 2)"
	echo "Building u-boot for device $UBOOT_AOSCNAME..."
	tar xf "$UBOOT_SRC"
	pushd "$UBOOT_DIR"
	for i in ../patches/u-boot/*
	do
		patch -Np1 -i $i
	done
	mkdir -p "$LOG_DIR"/u-boot-"$UBOOT_AOSCNAME"
	make "${UBOOT_CNAME}"_defconfig > "$LOG_DIR"/u-boot-"$UBOOT_AOSCNAME"/config.log 2>&1
	echo "Configured"
	make CROSS_COMPILE=/opt/abcross/armel/bin/armv7a-hardfloat-linux-gnueabi- > "$LOG_DIR"/u-boot-"$UBOOT_AOSCNAME"/build.log 2>&1
	echo "Built"
	mkdir -p "$OUT_DIR"/u-boot-"$UBOOT_AOSCNAME"/
	cp u-boot-sunxi-with-spl.bin "$OUT_DIR"/u-boot-"$UBOOT_AOSCNAME"/
	echo "Copied"
	popd
	rm -r "$UBOOT_DIR"
done

echo "Building linux..."

if [ "$BUILD_LINUX" != "0" ]; then
	echo "Building linux for KVM-disabled sunxi CPUs..."
	if [ ! -d "$LINUX_DIR" ]; then
		tar xf "$LINUX_SRC"
		pushd "$LINUX_DIR"
		for i in ../patches/linux/*
		do
			patch -Np1 -i $i
		done
	else
		pushd "$LINUX_DIR"
	fi
	mkdir -p "$LOG_DIR"/linux-sunxi-nokvm
	cp ../sunxi-nokvm-config .config
	echo "Configured"
	# FIXME: hard coded parallel.
	make ARCH=arm CROSS_COMPILE=/opt/abcross/armel/bin/armv7a-hardfloat-linux-gnueabi- -j5 > "$LOG_DIR"/linux-sunxi-nokvm/build.log 2>&1
	echo "Built"
	TMPDIR=$(mktemp -d)
	make ARCH=arm CROSS_COMPILE=/opt/abcross/armel/bin/armv7a-hardfloat-linux-gnueabi- INSTALL_MOD_PATH="$TMPDIR" modules_install > "$LOG_DIR"/linux-sunxi-nokvm/modules_install.log 2>&1
	mkdir -p "$OUT_DIR"/linux-sunxi-nokvm
	cp arch/arm/boot/zImage "$OUT_DIR"/linux-sunxi-nokvm/
	cp -r "$TMPDIR"/lib/modules/ "$OUT_DIR"/linux-sunxi-nokvm/
	rm -r "$TMPDIR"
	echo "Copied"
	echo "Building linux for KVM-enabled sunxi CPUs..."
	mkdir -p "$LOG_DIR"/linux-sunxi-kvm
	cp ../sunxi-kvm-config .config
	echo "Configured"
	make ARCH=arm CROSS_COMPILE=/opt/abcross/armel/bin/armv7a-hardfloat-linux-gnueabi- -j5 > "$LOG_DIR"/linux-sunxi-kvm/build.log 2>&1
	echo "Built"
	TMPDIR=$(mktemp -d)
	make ARCH=arm CROSS_COMPILE=/opt/abcross/armel/bin/armv7a-hardfloat-linux-gnueabi- INSTALL_MOD_PATH="$TMPDIR" modules_install > "$LOG_DIR"/linux-sunxi-kvm/modules_install.log 2>&1
	mkdir -p "$OUT_DIR"/linux-sunxi-kvm
	cp arch/arm/boot/zImage "$OUT_DIR"/linux-sunxi-kvm/
	cp -r "$TMPDIR"/lib/modules/ "$OUT_DIR"/linux-sunxi-kvm/
	rm -r "$TMPDIR"
	echo "Copied"
	popd
fi

echo "Building DTBs..."

[ "$BUILD_DTB" != "0" ] &&
for i in $DTB_TARGETS
do
	DTB_CNAME="$(echo $i | cut -d = -f 1)"
	DTB_AOSCNAME="$(echo $i | cut -d = -f 2)"
	mkdir -p "$OUT_DIR"/dtb-"$DTB_AOSCNAME"
	cp "$LINUX_DIR"/arch/arm/boot/dts/"$DTB_CNAME".dtb "$OUT_DIR"/dtb-"$DTB_AOSCNAME"/dtb.dtb
	echo "Copied dtb for $DTB_AOSCNAME"
done
