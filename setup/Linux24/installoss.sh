#!/bin/bash

if test "$USER " != "root "
then
  echo "You must be super-user or logged in as root to install OSS"
  exit 1
fi

echo "Installing Open Sound System `cat ./usr/lib/oss/version.dat`...."
echo "Copying files from ./etc and ./usr into /..."
tar -cpf - etc usr |(cd /; tar -xpf -)
echo "Running /usr/lib/oss/build/install script...."
if ! sh /usr/lib/oss/build/install.sh 
then
  echo
  echo "ERROR: install.sh script failed"
  exit 0
fi
echo "OSS installation complete..."
echo
echo "Run /sbin/soundon to start the drivers"
echo "Run /usr/bin/osstest to test the audio"
echo "Run /usr/bin/ossinfo to display status"
