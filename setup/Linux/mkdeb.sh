#!/bin/bash

VERSION=`sh showversion.sh`
RELEASE=`cat buildid.dat`
OSSNAME="oss-linux"

# Chosing the right architecture
if test `uname -m` = "x86_64"; then ARCH=amd64
else ARCH=`uname -m|sed 's/^i[3-9]86/i386/'`
fi

# Checking for known MD5 hasing programs
if type md5sum; then MD5=MD5SUM
elif type openssl; then MD5=OPENSSL
elif type md5; then MD5=MD5
elif type digest; then MD5=DIGEST
else echo "There has been no MD5 creation utily found. deb archive creation will be aborted." && exit 1
fi

DEBNAME=${OSSNAME}-${VERSION}_${RELEASE}_${ARCH}
echo building $DEBNAME.deb

mkdir control
echo "2.0" > debian-binary
echo "Package: " $OSSNAME > control/control
echo "Version: " $VERSION_$RELEASE >> control/control
echo "Section: sound" >> control/control
echo "Priority: optional" >> control/control
echo "Architecture: " $ARCH >> control/control
echo "Installed-Size: `du -ks prototype | awk '{print $1}'`" >> control/control
echo "Suggests: libsdl1.2debian-oss | libsdl1.2debian-all, libesd0, libwine-oss, libsox-fmt-oss, mpg123, gstreamer0.10-plugins-bad (>= 0.10.7), libasound2-plugins" >> control/control
echo "Maintainer: 4Front Technologies <support@opensound.com>" >> control/control
echo "Description: Open Sound System (http://www.opensound.com)
 OSS provides libraries and necessary drivers for practically all sound
  cards on the market including PnP and many PCI ones which enable you
  to play sound files, compose music, use MIDI (only included in the
  testing releases) and adjust your sound card using various user space
  programs." >> control/control

# Create the MD5 sums file using the program we have found earlier
case "$MD5" in
  MD5SUM)
    (cd prototype; find . -type f -exec sh -c 'i={}; i=${i#.}; md5sum ".$i"' \; > ../control/md5sums)
  ;;
  MD5)
    (cd prototype; find . -type f -exec sh -c 'i={}; i=${i#.}; x=`md5 ".$i" | awk "{ for (y=1;y<=NF;y++) if ((length(\\$y) == 32) && (\\$y !~ /[\/]/)) {print \\$y; break} }"`; echo "$x  $i"' \; > ../control/md5sums)
  ;;
  DIGEST)
    (cd prototype; find . -type f -exec sh -c 'i={}; i=${i#.}; x=`digest -a md5 ".$i"`; echo "$x  $i"' \; > ../control/md5sums)
  ;;
  OPENSSL)
    (cd prototype; find . -type f -exec sh -c 'i={}; i=${i#.}; x=`openssl md5 $i | awk "{ for (y=1;y<=NF;y++) if ((length(\\$y) == 32) && (\\$y !~ /[\/]/)) {print \\$y; break} }"`; echo "$x  $i"' \; > ../control/md5sums)
  ;;
esac

(cd prototype; find . -type f -print | sed 's/^.//g' | egrep "^/etc/" > ../control/conffiles)

rm -rf /tmp/prototype $DEBNAME.deb
cp -pRf prototype /tmp
cp setup/Linux/postinst setup/Linux/prerm setup/Linux/postrm control/
(cd control; tar cv * | gzip -9 > ../control.tar.gz)
(cd /tmp/prototype; tar cv ./* | gzip -9 > data.tar.gz)
mv /tmp/prototype/data.tar.gz .


ar r $DEBNAME.deb debian-binary control.tar.gz data.tar.gz

# Cleanup
rm -rf /tmp/prototype control control.tar.gz data.tar.gz debian-binary

if test -f 4front-private/export_package.sh
then
  sh 4front-private/export_package.sh $OSSNAME*.deb . `sh showversion.sh` /tmp `uname -i`-26
fi
