#!/bin/bash
if test `whoami` != "root"
then
  echo "You must be super-user or logged in as root to uninstall OSS..."
  exit 0
fi

echo "Uninstalling OSS...."
echo "Running soundoff...."
/usr/sbin/soundoff
echo "Restoring previously install sound drivers..."
sh /usr/lib/oss/scripts/restore_drv.sh
echo "Removing OSS Files in MANIFEST"
cd /
for i in `cat /usr/lib/oss/MANIFEST`
do
# echo "Removing file $i"
rm -f $i
done

echo "Removing /usr/lib/oss directory"
rm -rf /usr/lib/oss

echo "OSS Uninstalled. However you may need to reboot the system."
