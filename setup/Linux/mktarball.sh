#!/bin/sh

. ./.directories

VERSION=`sh showversion.sh`
RELEASE=`cat buildid.dat`
OSSNAME=oss-linux
PKGNAME=$OSSNAME-$VERSION-$RELEASE-`uname -m`

echo building $PKGNAME.tar.bz2
#cp ./setup/Linux/installoss.sh prototype
cp ./setup/Linux/removeoss.sh prototype/$OSSLIBDIR/scripts
(cd prototype; find . -type f -print) > prototype/$OSSLIBDIR/MANIFEST
(cd prototype; tar cfj /tmp/$PKGNAME.tar.bz2 . )
mv /tmp/$PKGNAME.tar.bz2 .

if test -f 4front-private/export_package.sh
then
  sh 4front-private/export_package.sh $PKGNAME.tar.bz2 . `sh showversion.sh` /tmp `uname -m`-26
fi

exit 0
