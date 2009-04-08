#!/bin/sh

# build script for ARM Linux (Nokia's Maemo plattform)

. ./.directories

KERNELDIR=~/maemo_kernel/kernel-source-diablo-2.6.21/kernel-source

rm -rf prototype

mkdir prototype
mkdir prototype/etc
echo "OSSLIBDIR=$OSSLIBDIR" > prototype/etc/oss.conf
mkdir prototype/usr
mkdir prototype/usr/bin
mkdir prototype/usr/sbin
mkdir -p prototype/$OSSLIBDIR
mkdir prototype/$OSSLIBDIR/etc
mkdir prototype/$OSSLIBDIR/conf.tmpl
mkdir prototype/$OSSLIBDIR/lib
mkdir prototype/$OSSLIBDIR/modules

cp target/bin/* prototype/usr/bin
cp target/sbin/* prototype/usr/sbin
cp target/lib/* prototype/$OSSLIBDIR/lib

# Compile the 64 bit div/mod functions required by gcc
rm -f bpabi.s bpabi_s.o bpabi_c.o
cc -c origdir/setup/Linux/arm/bpabi.c -o bpabi_c.o

for n in L_udivsi3 L_idivsi3 L_divsi3 L_aeabi_ldivmod L_aeabi_uldivmod L_dvmd_lnx
do
cpp -D$n origdir/setup/Linux/arm/lib1funcs.asm > $n.s
as -o $n.o $n.s
done

ld -r -o bpabi.o L*.o bpabi_c.o
rm -f L*.s L*.o bpabi_c.o

#build osscore

rm -rf tmp_build
mkdir tmp_build

cp origdir/setup/Linux/arm/Makefile.osscore.arm tmp_build/Makefile
cp origdir/setup/Linux/oss/build/osscore.c tmp_build/osscore.c

cp ./kernel/framework/include/*.h tmp_build/
cp ./kernel/OS/Linux/wrapper/wrap.h tmp_build/
cp ./setup/Linux/oss/build/ossdip.h tmp_build/
cp ./include/soundcard.h tmp_build/
cp ./kernel/framework/include/ossddk/oss_exports.h tmp_build/

if ! (cd tmp_build && make KERNELDIR=$KERNELDIR) > build.log 2>&1
then
   cat build.log
   echo
   echo Building osscore module failed
   exit 1
fi

ld -r tmp_build/osscore.ko target/objects/*.o bpabi.o -o prototype/usr/lib/oss/modules/osscore.ko

exit 0
