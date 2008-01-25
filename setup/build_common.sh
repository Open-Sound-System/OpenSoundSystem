#!/bin/sh

SRCDIR=$1

if test ! -d $SRCDIR
then
	echo Bad SRCDIR parameter
	exit 1
fi

if test ! -d prototype
then
	echo Bad prototype directory
	exit 1
fi

# Copy common files to the prototype tree
(cd $SRCDIR;tar cf - oss) | (cd prototype/usr/lib;tar xf -)
rm -f prototype/usr/lib/oss/.nomake

chmod 700 prototype/usr/sbin/*

exit 0
