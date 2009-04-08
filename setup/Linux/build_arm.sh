#!/bin/sh

# build script for ARM Linux (Nokia's Maemo plattform)

. ./.directories

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

exit 0
