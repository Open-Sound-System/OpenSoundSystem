#!/bin/sh

. ./.directories

rm -rf prototype

mkdir prototype
mkdir prototype/etc
echo OSSLIBDIR=/usr/lib/oss > prototype/etc/oss.conf
mkdir prototype/usr
mkdir prototype/usr/lib
mkdir prototype/usr/bin
mkdir prototype/usr/share
mkdir prototype/usr/share/man
mkdir prototype/usr/share/man/man1
mkdir prototype/usr/share/man/man7
mkdir prototype/usr/share/man/man8
mkdir prototype/usr/sbin
mkdir prototype/usr/lib/oss
mkdir prototype/usr/lib/oss/etc
mkdir prototype/usr/lib/oss/save
mkdir prototype/usr/lib/oss/conf.tmpl
mkdir prototype/usr/lib/oss/lib
mkdir prototype/usr/lib/oss/modules.regparm
mkdir prototype/usr/lib/oss/modules.noregparm
mkdir prototype/usr/lib/oss/objects.regparm
mkdir prototype/usr/lib/oss/objects.noregparm
mkdir prototype/usr/lib/oss/include
mkdir prototype/usr/lib/oss/include/sys
mkdir prototype/usr/lib/oss/include/internals
mkdir prototype/usr/lib/oss/build

chmod 700 prototype/usr/lib/oss/modules.*
chmod 700 prototype/usr/lib/oss/objects.*
chmod 700 prototype/usr/lib/oss/build
chmod 700 prototype/usr/lib/oss/save

if test "`cat regparm` " = "1 "
then
  MODULES=modules.regparm
  OBJECTS=objects.regparm
else
  MODULES=modules.noregparm
  OBJECTS=objects.noregparm
fi

cp .version prototype/usr/lib/oss/version.dat

if ! test -f regparm
then
  echo Error: ./regparm is missing
  exit -1
fi

cp regparm prototype/usr/lib/oss/build

# Regenerating the config file templates
rm -f /tmp/confgen
if ! cc -o /tmp/confgen ./setup/Linux/confgen.c
then
	echo Building confgen failed
	exit -1
fi

if ! /tmp/confgen prototype/usr/lib/oss/conf.tmpl \\/usr\\/lib\\/oss\\/conf kernel/drv/* kernel/nonfree/drv/* kernel/framework/*
then
	echo Running confgen failed
	exit -1
fi

rm -f /tmp/confgen

cp -a $SRCDIR/include/* prototype/usr/lib/oss/include/sys/
cp $SRCDIR/kernel/framework/include/midiparser.h prototype/usr/lib/oss/include/
cp -f $SRCDIR/kernel/OS/Linux/wrapper/wrap.h prototype/usr/lib/oss/build/
cp -f $SRCDIR/kernel/framework/include/udi.h prototype/usr/lib/oss/build/
cp -a $SRCDIR/kernel/framework/include/*_core.h kernel/framework/include/local_config.h prototype/usr/lib/oss/include/internals
cp -a $SRCDIR/kernel/framework/include/ossddk prototype/usr/lib/oss/include/sys
cp kernel/framework/include/timestamp.h prototype/usr/lib/oss/include/internals

cat > prototype/usr/lib/oss/include/internals/WARNING.txt << EOF
Caution: All header files included in this directory are there only because
         some parts of OSS may need to be re-compiled. It is not safe to use
         these files for any purposes because they will change between OSS
         versions/builds.
EOF

cp -f target/build/* prototype/usr/lib/oss/build/
cp -f target/bin/* prototype/usr/bin
cp -f target/sbin/* prototype/usr/sbin

cp -a $SRCDIR/setup/Linux/oss prototype/usr/lib
cp -a $SRCDIR/setup/Linux/sbin prototype/usr/

ld -r -o prototype/usr/lib/oss/$OBJECTS/osscore.o target/objects/*.o

rm -f devlist.txt devices.list

for n in `find kernel/ -name .devices`
do
  cat $n >> devices.list
done

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

# Link the optional NOREGPARM modules
if test -d noregparm
then
   ld -r -o prototype/usr/lib/oss/objects.noregparm/osscore.o noregparm/target/objects/*.o

   for n in noregparm/target/modules/*.o
   do
	N=`basename $n .o`
	ld -r -o prototype/usr/lib/oss/modules.noregparm/$N.o $n
   done
fi

for n in $SRCDIR/misc/man7/*.man
do
	N=`basename $n .man`

	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 $n |gzip -9 > prototype/usr/share/man/man7/$N.7.gz
done

for n in $SRCDIR/misc/man1m/*.man
do
	N=`basename $n .man`
	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS System Administration Commands" -s 1 $n |gzip -9 > prototype/usr/share/man/man1/$N.1.gz
done

if ! cp lib/libOSSlib/libOSSlib.so lib/libsalsa/.libs/libsalsa.so.2.0.0 prototype/usr/lib/oss/lib
then
  echo Warning: No libsalsa library compiled
fi

cp devlist.txt prototype/usr/lib/oss/etc/devices.list

if test -d kernel/nonfree
then
	sed 's/.*	//' <  devlist.txt|sort|uniq >$SRCDIR/devlists/Linux
	#cp devlist.txt $SRCDIR/devlists/Linux
fi

# Generate Man pages for commands
for i in target/bin/*
do
CMD=`basename $i`
$SRCDIR/setup/txt2man -t "$CMD" -v "OSS User Commands" -s 1 cmd/$CMD/$CMD.man|gzip -9 > prototype/usr/share/man/man1/$CMD.1.gz
echo done $CMD
done

for i in target/sbin/*
do
  CMD=`basename $i`
  if test -f cmd/$CMD/$CMD.man
  then
	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS System Administration Commands" -s 8 cmd/$CMD/$CMD.man|gzip -9 > prototype/usr/share/man/man8/$CMD.8.gz
	echo done $CMD
  fi
done

rm -f prototype/usr/share/man/man8/ossdetect.8
$SRCDIR/setup/txt2man -t "ossdetect" -v "User Commands" -s 8 os_cmd/Linux/ossdetect/ossdetect.man|gzip -9 > prototype/usr/share/man/man8/ossdetect.8.gz
echo done ossdetect

# Hal 0.50+ hotplug
mkdir -p prototype/usr/lib/hal/scripts
ln -s /usr/lib/oss/scripts/oss_usb-create-devices prototype/usr/lib/hal/scripts/
mkdir -p prototype/usr/share/hal/fdi/policy/20thirdparty/
ln -s /usr/lib/oss/scripts/90-oss_usb-create-device.fdi prototype/usr/share/hal/fdi/policy/20thirdparty/

# Licensing stuff
if test -f $SRCDIR/4front-private/osslic.c
then
	cc -o prototype/usr/sbin/osslic -Isetup -Ikernel/nonfree/include -Ikernel/framework/include -Iinclude -Ikernel/OS/Linux -I$SRCDIR $SRCDIR/4front-private/osslic.c
	strip prototype/usr/sbin/osslic
	
	BITS=3 # Default to 32 bit ELF format
	if test "`uname -m` " = "x86_64 "
	then
	   BITS=6 # Use 64 bit ELF format
	fi
	prototype/usr/sbin/osslic -q -u -$BITS./prototype/usr/lib/oss/objects.regparm/osscore.o
	prototype/usr/sbin/osslic -q -u -$BITS./prototype/usr/lib/oss/objects.noregparm/osscore.o
	
fi

if test -f 4front-private/ossupdate.c
then
  #ossupdate
  cc -I. 4front-private/ossupdate.c -s -o prototype/usr/sbin/ossupdate
fi

chmod 700 prototype/usr/sbin/*
chmod 755 prototype/usr/bin/*

(cd prototype;ls usr/sbin/* usr/bin/* etc/* usr/share/man/man*/* > usr/lib/oss/sysfiles.list)

exit 0
