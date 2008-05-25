#!/bin/sh

. ./.directories

rm -rf prototype

mkdir prototype
mkdir prototype/etc
mkdir prototype/etc/rc.d
mkdir prototype/usr
mkdir prototype/usr/bin
mkdir prototype/usr/sbin
mkdir prototype/usr/lib
mkdir prototype/usr/lib/oss
mkdir prototype/usr/lib/oss/etc
mkdir prototype/usr/lib/oss/lib
mkdir prototype/usr/lib/oss/include
mkdir prototype/usr/lib/oss/include/sys
mkdir prototype/usr/lib/oss/logs
mkdir prototype/usr/lib/oss/modules
mkdir prototype/usr/lib/oss/objects
mkdir prototype/usr/share
mkdir prototype/usr/share/man
mkdir prototype/usr/share/man/man1
mkdir prototype/usr/share/man/man8

echo "OSSLIBDIR=/usr/lib/oss" > prototype/etc/oss.conf

cp -r $SRCDIR/setup/FreeBSD/oss prototype/usr/lib/
cp $SRCDIR/kernel/OS/FreeBSD/wrapper/bsddefs.h prototype/usr/lib/oss/build/
cp $SRCDIR/kernel/framework/include/ossddk/oss_exports.h prototype/usr/lib/oss/build/

cp $SRCDIR/include/soundcard.h prototype/usr/lib/oss/include/sys/
cp $SRCDIR/lib/libOSSlib/midiparser.h prototype/usr/lib/oss/include/
cp kernel/framework/include/timestamp.h prototype/usr/lib/oss/build/

ld -r -o prototype/usr/lib/oss/build/osscore.lib target/objects/*.o

rm -f devlist.txt

for n in target/modules/*.o
do
	N=`basename $n .o`
echo Check devices for $N
  	grep "^$N[ 	]" ./devices.list >> devlist.txt
done

cp target/modules/*.o prototype/usr/lib/oss/objects
cp target/build/*.c prototype/usr/lib/oss/build/
cp target/bin/* prototype/usr/bin/
cp target/sbin/* prototype/usr/sbin/
cp $SRCDIR/setup/FreeBSD/sbin/* prototype/usr/sbin/
cp $SRCDIR/setup/FreeBSD/etc/rc.d/oss prototype/etc/rc.d
cp lib/libOSSlib/libOSSlib.so prototype/usr/lib/oss/lib

cp devlist.txt prototype/usr/lib/oss/etc/devices.list

rm -f $SRCDIR/devlists/FreeBSD

cp devlist.txt $SRCDIR/devlists/FreeBSD

# Generate Man pages for commands
for i in target/bin/*
do
CMD=`basename $i`
$SRCDIR/setup/txt2man -t "$CMD" -v "User Commands" -s 1 cmd/$CMD/$CMD.man > prototype/usr/share/man/man1/$CMD.1
echo done $CMD
done

for i in target/sbin/*
do
  CMD=`basename $i`
  if test -f cmd/$CMD/$CMD.man
  then
	$SRCDIR/setup/txt2man -t "$CMD" -v "System Administration Commands" -s 8 cmd/$CMD/$CMD.man > prototype/usr/share/man/man8/$CMD.8
	echo done $CMD
  fi
done

for i in $SRCDIR/misc/man1m/*.man
do
        N=`basename $i .man`
        $SRCDIR/setup/txt2man -t "$CMD" -v "OSS System Administration Commands" -s 1 $i > prototype/usr/share/man/man1/$N.1
done

rm -f prototype/usr/share/man/man8/ossdetect.8
$SRCDIR/setup/txt2man -t "ossdetect" -v "User Commands" -s 8 os_cmd/FreeBSD/ossdetect/ossdetect.man > prototype/usr/share/man/man8/ossdetect.8
echo done ossdetect

for n in target/modules/*.o
do
	N=`basename $n .o`
	ld -r -o prototype/usr/lib/oss/$MODULES/$N.o $n
	echo Check devices for $N
  	grep "^$N[ 	]" ./devices.list >> devlist.txt

	rm -f /tmp/ossman.txt

	if test -f $SRCDIR/kernel/drv/$N/$N.man
	then
	  sed 's/CONFIGFILEPATH/\/usr\/lib\/oss\/conf/' < $SRCDIR/kernel/drv/$N/$N.man > /tmp/ossman.txt
	  $SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 /tmp/ossman.txt|gzip -9 > prototype/usr/share/man/man7/$N.7.gz
	else
		if test -f $SRCDIR/kernel/nonfree/drv/$N/$N.man
		then
	  		sed 's/CONFIGFILEPATH/\/usr\/lib\/oss\/conf/' < $SRCDIR/kernel/nonfree/drv/$N/$N.man > /tmp/ossman.txt
	  		$SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 $SRCDIR/kernel/nonfree/drv/$N/$N.man|gzip -9 > prototype/usr/share/man/man7/$N.7.gz
		fi
	fi
done

sed 's/CONFIGFILEPATH/\/usr\/lib\/oss\/conf/' < $SRCDIR/kernel/drv/osscore/osscore.man > /tmp/ossman.txt
$SRCDIR/setup/txt2man -t "osscore" -v "OSS Devices" -s 7 /tmp/ossman.txt|gzip -9 > prototype/usr/share/man/man7/osscore.7.gz
rm -f /tmp/ossman.txt

cp .version prototype/usr/lib/oss/version.dat

# Licensing stuff
if test -f $SRCDIR/4front-private/osslic.c
then
	cc -o prototype/usr/sbin/osslic -Isetup -Ikernel/nonfree/include -Ikernel/framework/include -Iinclude -Ikernel/OS/FreeBSD -I$SRCDIR $SRCDIR/4front-private/osslic.c
	strip prototype/usr/sbin/osslic

        BITS=3 # Default to 32 bit ELF format
        if test "`uname -m` " = "amd64 "
        then
           BITS=6 # Use 64 bit ELF format
        fi

	prototype/usr/sbin/osslic -q -u -$BITS./prototype/usr/lib/oss/build/osscore.lib
	
fi

if test -f 4front-private/ossupdate.c
then
  #ossupdate
  cc -I. 4front-private/ossupdate.c -s -o prototype/usr/sbin/ossupdate
fi

chmod 700 prototype/usr/sbin/*
chmod 755 prototype/usr/bin/*
chmod 700 prototype/usr/lib/oss

(cd prototype;ls usr/sbin/* usr/bin/* etc/* usr/share/man/man*/* > usr/lib/oss/sysfiles.list)

exit 0
