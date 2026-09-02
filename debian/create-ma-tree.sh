#!/bin/bash
# Extract OSS4.2 core & drivers modules.
# based on http://www.arklinux.org/~bero/oss2kconfig

usage() {
	[ -z "$1" ] || echo $1
	echo "Usage:"
	echo "$0 PATH_TO_NEWDIR PATH_TO_OSS"
	exit 1
}

[ "$#" != 2 ] && usage
#[ ! -f "$1"/Kbuild ] && usage "$1 does not appear to be a kernel source tree"
[ ! -d "$2"/oss ] && usage "$2 does not appear to be a OSS source tree"

NEWDIR=`pwd`"/$1"
OSSDIR=`pwd`"/$2"

mkdir -p $NEWDIR/core $NEWDIR/drivers $NEWDIR/include

cd $OSSDIR

# Remove sparc/solaris stuff
rm -rf kernel/drv/osscore
rm -rf kernel/drv/oss_audiocs
rm -rf kernel/drv/oss_sadasupport

# Move bits into the kernel tree...
cp -L include/soundcard.h "$NEWDIR"/include
cp -L include/oss_userdev_exports.h "$NEWDIR"/include

cp -L kernel/OS/Linux/*.[ch] "$NEWDIR"/core/
cp -L kernel/OS/Linux/wrapper/*.[ch] "$NEWDIR"/core/
cp -L setup/Linux/oss/build/*.inc "$NEWDIR"/drivers/
cp -L setup/Linux/oss/build/* "$NEWDIR"/core/
rm "$NEWDIR"/core/*.inc
rm "$NEWDIR"/core/install.sh
cp -rL kernel/framework/include/* "$NEWDIR"/core/
mv "$NEWDIR"/core/osscore.c "$NEWDIR"/core/oss_core.c

SOURCES=""
SRCCOUNT=0
for i in ac97 audio midi midi_stubs mixer osscore remux sndstat uart401 vmix_core; do
	i="kernel/framework/$i"
	[ -d "$i" ] || continue
	NAME="`basename $i`"
	pushd "$i"
	for j in `ls *.c`; do
		SOURCES="$SOURCES $j"
		SRCCOUNT=$((SRCCOUNT + 1))
	done
	popd
	rm -f "$i"/*.man "$i"/Makefile
	cp -L "$i"/* "$NEWDIR"/core/
done
OBJS="os_linux.o `echo $SOURCES |sed -e 's,\.c,.o,g'`"
cat >"$NEWDIR"/core/Makefile <<EOF
MULTIARCH_PATH = /usr/include/\$(shell dpkg-architecture -qDEB_HOST_MULTIARCH)
ccflags-y = -I\$(KBUILD_EXTMOD) -isystem /usr/include -isystem \$(MULTIARCH_PATH) \
	-isystem \$(shell \$(CC) --print-file-name=include-fixed)
obj-m += osscore.o
osscore-objs := oss_core.o $OBJS
EOF

# This is somewhat ugly, but unavoidable -- parts of OSS need those defines and
# access to system includes while osscore.c can't be compiled with them...
for i in $OBJS; do
	echo "CFLAGS_$i = -D_KERNEL" >>"$NEWDIR"/core/Makefile
done


cat >"$NEWDIR"/drivers/Makefile <<EOF
osscore_symbols.inc:
	echo "static const struct modversion_info ____versions[]" >> osscore_symbols.inc
	echo " __attribute__((used))" >> osscore_symbols.inc
	echo "__attribute__((section(\"__versions\"))) = {" >> osscore_symbols.inc
	sed -e "s:^:{:" -e "s:\t:, \":" -e "s:\t\(.\)*:\"},:" < \$(PWD)/core/Module.symvers >> osscore_symbols.inc
	echo "};" >> osscore_symbols.inc

KBUILD_EXTRA_SYMBOLS := \$(PWD)/core/Module.symvers

MULTIARCH_PATH = /usr/include/\$(shell dpkg-architecture -qDEB_HOST_MULTIARCH)
ccflags-y=-D_KERNEL -I\$(KBUILD_EXTMOD)/../core -I\$(KBUILD_EXTMOD) -isystem /usr/include -isystem \$(MULTIARCH_PATH) \
	-isystem \$(shell \$(CC) --print-file-name=include-fixed)
EOF

# Merge the drivers...
for i in kernel/drv/*; do
	[ -d "$i" ] || continue
	[ "$i" != kernel/drv/lynxtwo ] || continue
	NAME="`basename $i`"
	DEV="`echo $NAME | tr a-z A-Z`"
	pushd $i
	SOURCES=""
	SRCCOUNT=0
	for j in `ls *.c`; do
		if [ "$j" = "$NAME.c" ]; then
			mv $j ${NAME}driver.c
			SOURCES="$SOURCES ${NAME}driver.c"
		else
			SOURCES="$SOURCES $j"
		fi
		SRCCOUNT=$((SRCCOUNT + 1))
	done
	popd
	if [ -e target/build/"$NAME".c ]; then
		cp target/build/"$NAME".c "$NEWDIR"/drivers/${NAME}module.c
		SOURCES="$SOURCES ${NAME}module.c"
		SRCCOUNT=$((SRCCOUNT + 1))
	fi
	
	rm -f "$i"/*.man "$i"/Makefile
	cp -L "$i"/* "$NEWDIR"/drivers/
	if [ "$SRCCOUNT" = "1" ]; then
		echo "obj-m += ${SOURCES/.c/.o}" >>"$NEWDIR"/drivers/Makefile
	else
		echo "obj-m += $NAME.o" >>"$NEWDIR"/drivers/Makefile
		echo "$NAME-objs := `echo $SOURCES |sed -e 's,\.c,.o,g'`" >>"$NEWDIR"/drivers/Makefile
	fi
done
rm -rf "$NEWDIR"/drivers/*.o


# Main Makefile
#echo 'obj-m += core/ drivers/ ' > "$NEWDIR"/Makefile

# Adjust OSS files to some changed header file locations
find "$NEWDIR"/ -type f |xargs sed -i -e 's,"soundcard.h","../include/soundcard.h",g;s,../include/internals/,,g;s,"../include/sys/soundcard.h","../include/soundcard.h",g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,<oss_userdev_exports.h>,"../include/oss_userdev_exports.h",g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,"oss_exports.h",<ossddk/oss_exports.h>,g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,"ossddk.h",<ossddk/ossddk.h>,g'
sed -i -e 's,kernel/OS/Linux/wrapper/wrap.h,wrap.h,g' "$NEWDIR"/core/os.h
sed -i -e 's,#include "ubuntu_version_hack.inc",,g' "$NEWDIR"/core/oss_core.c

# Don't trust OSS_LITTLE_ENDIAN & OSS_BIG_ENDIAN
sed -i -e '/^#define AUDIO_CORE_H/a\#include <endian.h>' "$NEWDIR"/core/audio_core.h
find "$NEWDIR"/ -type f |xargs sed -i -e 's,#ifdef OSS_BIG_ENDIAN,#if __BYTE_ORDER == __BIG_ENDIAN,g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,#ifndef OSS_BIG_ENDIAN,#if __BYTE_ORDER != __BIG_ENDIAN,g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,#ifdef OSS_LITTLE_ENDIAN,#if __BYTE_ORDER == __LITTLE_ENDIAN,g'
find "$NEWDIR"/ -type f |xargs sed -i -e 's,#ifndef OSS_LITTLE_ENDIAN,#if __BYTE_ORDER != __LITTLE_ENDIAN,g'

