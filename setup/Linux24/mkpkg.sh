#!/bin/bash

VERSION=`sh ../../showversion.sh`
RELEASE=`cat ../../buildid.dat`

OSSNAME=oss-linux

RPMNAME=$OSSNAME-$VERSION

echo building $RPMNAME.rpm

rm -rf spec $RPMNAME
mkdir $RPMNAME
echo "Version: " $VERSION > spec
echo "Release: " $RELEASE >> spec
echo "Name: " $OSSNAME >> spec
cat spec.tmpl >> spec
echo "%files" >> spec
(cd ../../prototype; find . -type f -print | sed 's/^.//g' > /tmp/filelist)
cat /tmp/filelist >> spec
rm -rf /tmp/prototype
cp -af ../../prototype /tmp
tar zcvf /tmp/oss $RPMNAME
rpmbuild -bb --define "_sourcedir /tmp" --define "_rpmdir ../.." --define '_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm' spec
# Cleanup
rm -rf /tmp/oss /tmp/filelist $RPMNAME
