#!/bin/sh
VERSION=`sh showversion.sh`
BUILDID=`cat buildid.dat`
TOPDIR=`pwd`
SETUPDIR=$TOPDIR/setup/FreeBSD
PROTODIR=$TOPDIR/prototype
KERNELVERS=`uname -m`
BSDVER=`uname -r | cut -s -d'.' -f1`
PKGNAME=oss-freebsd$BSDVER-$VERSION-$BUILDID-$KERNELVERS

(cd $PROTODIR; find . -type f -print  > $SETUPDIR/pkg-plist)

(cd /; pkg_create -c $SETUPDIR/pkg-comment -d $SETUPDIR/pkg-descr -I $SETUPDIR/pkg-postinstall -k $SETUPDIR/pkg-preremove -K $SETUPDIR/pkg-postremove -f $SETUPDIR/pkg-plist -p / -S $PROTODIR -v $TOPDIR/$PKGNAME.tbz)

if test -f 4front-private/export_package.sh
then
  sh 4front-private/export_package.sh $PKGNAME.tbz . `sh showversion.sh` /tmp `uname -m`
fi
