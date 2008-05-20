#!/bin/bash

VERSION=`sh showversion.sh`
RELEASE=`cat buildid.dat`
ARCH=`uname -m`
OSSNAME=oss-linux

DEBNAME=${OSSNAME}-${VERSION}_${RELEASE}_${ARCH}
echo building $DEBNAME.deb

mkdir control
echo "2.0" > debian-binary
echo "Package: " $OSSNAME > control/control
echo "Version: " $VERSION_$RELEASE >> control/control
echo "Section: sound" >> control/control
echo "Priority: optional" >> control/control
echo "Architecture: " $ARCH >> control/control
echo "Maintainer: Hannu Savolainen <hannu@opensound.com>" >> control/control
echo "Description: Open Sound System
 OSS provides libraries and necessary drivers which enable you to play
  sound files, compose music, use MIDI (only included in the testing
  releases) and adjust your sound card using various user space programs." >> control/control

(cd prototype; find . -type f -print | sed 's/^.//g' | md5sum > ../control/md5sums)
(cd prototype; find . -type f -print | sed 's/^.//g' | grep -E "^etc/" > ../control/conffiles)
rm -rf /tmp/prototype
cp -af prototype /tmp
tar zcvf control.tar.gz control/*
(cd /tmp/prototype; tar zcvf data.tar.gz ./*)
mv /tmp/prototype/data.tar.gz .

ar q $DEBNAME.deb control.tar.gz data.tar.gz debian-binary

# Cleanup
rm -rf /tmp/prototype control control.tar.gz data.tar.gz debian-binary

if test -f 4front-private/export_package.sh
then
  sh 4front-private/export_package.sh $OSSNAME*.deb . `sh showversion.sh` /tmp `uname -i`-26
fi
