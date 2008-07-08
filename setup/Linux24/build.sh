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
mkdir prototype/usr/lib/oss/logs
mkdir prototype/usr/lib/oss/modules
mkdir prototype/usr/lib/oss/objects
mkdir prototype/usr/lib/oss/include
mkdir prototype/usr/lib/oss/include/sys
mkdir prototype/usr/lib/oss/include/internals
mkdir prototype/usr/lib/oss/build

chmod 700 prototype/usr/lib/oss/modules
chmod 700 prototype/usr/lib/oss/objects
chmod 700 prototype/usr/lib/oss/build
chmod 700 prototype/usr/lib/oss/save

MODULES=modules
OBJECTS=objects

cp .version prototype/usr/lib/oss/version.dat

# Regenerating the config file templates
rm -f /tmp/confgen
if ! cc -o /tmp/confgen ./setup/Linux24/confgen.c
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
cp $SRCDIR/lib/libOSSlib/midiparser.h prototype/usr/lib/oss/include/
cp -f $SRCDIR/kernel/OS/Linux24/wrapper/wrap.h prototype/usr/lib/oss/build/
cp -f $SRCDIR/kernel/framework/include/udi.h prototype/usr/lib/oss/build/
cp -a $SRCDIR/kernel/framework/include/*_core.h prototype/usr/lib/oss/include/internals
cp -a kernel/framework/include/timestamp.h prototype/usr/lib/oss/include/internals

cat > prototype/usr/lib/oss/include/internals/WARNING.txt << EOF
Caution: All header files included in this directory are there only because
         some parts of OSS may need to be re-compiled. It is not safe to use
         these files for any purposes because they will change between OSS
         versions/builds.
EOF

cp -f target/build/* prototype/usr/lib/oss/build/
cp -f target/bin/* prototype/usr/bin
cp -f target/sbin/* prototype/usr/sbin

cp -a $SRCDIR/oss prototype/usr/lib
cp -a $SRCDIR/setup/Linux24/oss prototype/usr/lib
cp -a $SRCDIR/setup/Linux24/sbin prototype/usr/

ld -r -o prototype/usr/lib/oss/$OBJECTS/osscore.o target/objects/*.o

rm -f devlist.txt

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
	  $SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 /tmp/ossman.txt|gzip > prototype/usr/share/man/man7/$N.7.gz
	else
		if test -f $SRCDIR/kernel/nonfree/drv/$N/$N.man
		then
	  		sed 's/CONFIGFILEPATH/\/usr\/lib\/oss\/conf/' < $SRCDIR/kernel/nonfree/drv/$N/$N.man > /tmp/ossman.txt
	  		$SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 $SRCDIR/kernel/nonfree/drv/$N/$N.man|gzip > prototype/usr/share/man/man7/$N.7.gz
		fi
	fi

	rm -f /tmp/ossman.txt
done

for n in $SRCDIR/misc/man7/*.man
do
	N=`basename $n .man`

	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS Devices" -s 7 $n |gzip > prototype/usr/share/man/man7/$N.7.gz
done

for n in $SRCDIR/misc/man1m/*.man
do
	N=`basename $n .man`
	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS System Administration Commands" -s 1 $n |gzip > prototype/usr/share/man/man1/$N.1.gz
done

if ! cp lib/libOSSlib/libOSSlib.so lib/libsalsa/.libs/libsalsa.so.2.0.0 prototype/usr/lib/oss/lib
then
  echo Warning: No libsalsa library compiled
fi

cp devlist.txt prototype/usr/lib/oss/etc/devices.list

if test -d kernel/nonfree
then
	sed 's/.*	//' <  devlist.txt|sort|uniq >$SRCDIR/devlists/Linux24
	#cp devlist.txt $SRCDIR/devlists/Linux24
fi

# Generate Man pages for commands
for i in target/bin/*
do
CMD=`basename $i`
$SRCDIR/setup/txt2man -t "$CMD" -v "OSS User Commands" -s 1 cmd/$CMD/$CMD.man|gzip > prototype/usr/share/man/man1/$CMD.1.gz
echo done $CMD
done

for i in target/sbin/*
do
  CMD=`basename $i`
  if test -f cmd/$CMD/$CMD.man
  then
	$SRCDIR/setup/txt2man -t "$CMD" -v "OSS System Administration Commands" -s 8 cmd/$CMD/$CMD.man|gzip > prototype/usr/share/man/man8/$CMD.8.gz
	echo done $CMD
  fi
done

rm -f prototype/usr/share/man/man8/ossdetect.8
$SRCDIR/setup/txt2man -t "ossdetect" -v "User Commands" -s 8 os_cmd/Linux24/ossdetect/ossdetect.man|gzip > prototype/usr/share/man/man8/ossdetect.8.gz
echo done ossdetect

# Licensing stuff
if test -f $SRCDIR/4front-private/osslic.c
then
	cc -o prototype/usr/sbin/osslic -Isetup -Ikernel/nonfree/include -Ikernel/framework/include -Iinclude -Ikernel/OS/Linux24 -I$SRCDIR $SRCDIR/4front-private/osslic.c
	strip prototype/usr/sbin/osslic
	
	BITS=3 # Default to 32 bit ELF format
	if test "`uname -m` " = "x86_64 "
	then
	   BITS=6 # Use 64 bit ELF format
	fi
	prototype/usr/sbin/osslic -q -u -$BITS ./prototype/usr/lib/oss/objects/osscore.o
	
fi

if test -f 4front-private/ossupdate.c
then
  #ossupdate
  cc -I. 4front-private/ossupdate.c -s -o prototype/usr/sbin/ossupdate
fi

chmod 700 prototype/usr/sbin/*
chmod 755 prototype/usr/bin/*

(cd prototype;ls usr/sbin/* usr/bin/* etc/* usr/share/man/man*/* > usr/lib/oss/sysfiles.list)

exec sh $SRCDIR/setup/build_common.sh $SRCDIR

exit 0
