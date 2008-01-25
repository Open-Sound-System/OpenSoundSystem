#!/bin/sh
if test -f /etc/oss.conf
then
  . /etc/oss.conf
else
  OSSLIBDIR=/usr/lib/oss
fi

cd $OSSLIBDIR/build

rm -f $OSSLIBDIR/.cuckoo_installed

# Check if we should use REGPARM or non-REGPARM modules
if /usr/sbin/ossvermagic -r || /sbin/modinfo ext3|grep -q REGPARM
then
   REGPARM=REGPARM
   rm -rf $OSSLIBDIR/objects
   ln -s $OSSLIBDIR/objects.regparm $OSSLIBDIR/objects
   rm -rf $OSSLIBDIR/modules
   ln -s $OSSLIBDIR/modules.regparm $OSSLIBDIR/modules
else
   REGPARM=NOREGPARM
   rm -rf $OSSLIBDIR/objects
   ln -s $OSSLIBDIR/objects.noregparm $OSSLIBDIR/objects
   rm -rf $OSSLIBDIR/modules
   ln -s $OSSLIBDIR/modules.noregparm $OSSLIBDIR/modules
fi

if ! test -f $OSSLIBDIR/objects/osscore.o
then
	echo Error: OSS core module for $REGPARM kernel is not available
	exit 1
fi

if ! test -f $OSSLIBDIR/modules/ich.o
then
	echo Error: OSS driver modules for $REGPARM kernel are not available
	exit 1
fi

echo
echo OSS build environment set up for $REGPARM kernels

KERNELDIR=/lib/modules/`uname -r`/build
UBUNTUPACKAGES=""

OK=1
echo

if test "`which gcc 2>/dev/null` " = " "
then
  echo "    gcc"
  UBUNTUPACKAGES="$UBUNTUPACKAGES gcc"
  OK=0
fi

if test "`which make  2>/dev/null` " = " "
then
  echo "    make"
  UBUNTUPACKAGES="$UBUNTUPACKAGES make"
  OK=0
fi

if test "`which ld  2>/dev/null` " = " "
then
  echo "    binutils"
  UBUNTUPACKAGES="$UBUNTUPACKAGES binutils"
  OK=0
fi

if ! test -f /usr/include/stdio.h
then
  echo "    C library headers (glibc-devel or build-essential)"
  OK=0
  UBUNTUPACKAGES="$UBUNTUPACKAGES build-essentials"
fi

if test "$OK " = "0 "
then
  echo
  echo 'Error: The above Linux package(s) seem to be missing from your system.'
  echo '       Please install them and then try to install OSS again.'
  echo
  echo Please refer to the documentation of your Linux distribution if you
  echo have problems with installing the packages.
  echo

  if grep -q Ubuntu /etc/issue # Ubuntu?
  then
    echo You can use the following commands to download and install all
    echo required packages:
    echo

    for n in $UBUNTUPACKAGES
    do
	echo "  apt-get install $n"
    done

    exit 1
  fi

  exit 1
fi


if ! test -f $KERNELDIR/Makefile && ! test -f /lib/modules/`uname -r`/sources/Makefile
then
  echo
  echo 'Warning: Cannot locate the Linux kernel development package for'
  echo '         Linux kernel version ' `uname -r`
  echo '         Please install the kernel development package if linking the'
  echo '         OSS modules fails.'
  echo
  echo The kernel development package may be called kernel-devel, kernel-smp-devel,
  echo kernel-sources, kernel-headers or something like that. Please refer
  echo to the documentation of your Linux distribution if there are any
  echo difficulties in installing the kernel/driver development environment.
  echo

  if grep -q 'Fedora Core release' /etc/issue
  then
	if uname -v|grep -q SMP	
	then
	  echo Assuming that you are using Fedora Core 5 or later
	  echo "the right kernel source package (RPM) is probably called"
	  echo kernel-smp-devel.
	else
	  echo Assuming that you are using Fedora Core 5 or later
	  echo "the right kernel source package (RPM) is probably called"
	  echo kernel-devel.
	fi
  else
	echo For your Linux distribution the right kernel source package
	echo might be kernel-source.
  fi
  echo

  if grep -q Ubuntu /etc/issue # Ubuntu?
  then
	echo Under Ubuntu you may need to prepare the kernel environment
	echo after downloading the kernel sources using
	echo 
	echo "  sudo apt-get install linux-headers-`uname -r`"
        echo "  cd /usr/src/linux-headers-`uname -r`/"
        echo "  sudo make prepare"
        echo "  sudo make prepare scripts"
	echo
  fi
fi

if ! test -d /lib/modules/`uname -r`
then
	echo Error: Kernel directory /lib/modules/`uname -r` does not exist
	exit 1
fi

cp -f ../objects/osscore.o osscore_mainline.o

ln -sf ../include/sys/*.h ../include/sys/ossddk .

rm -f Makefile
ln -s Makefile.osscore Makefile

if ! test -d $OSSLIBDIR/logs
then
	mkdir $OSSLIBDIR/logs
fi

echo Building module osscore

if ! make KERNELDIR=$KERNELDIR> build.list 2>&1
then
	echo Failed to compile OSS
	cat build.list
	exit 2
fi

if ! test -d /lib/modules/`uname -r`/kernel/oss
then
  mkdir /lib/modules/`uname -r`/kernel/oss
fi

if ! test -d /lib/modules/`uname -r`/kernel/oss
then
	echo OSS module directory /lib/modules/`uname -r`/kernel/oss does not exist.
	exit 3
fi

if ! ld -r osscore.ko osscore_mainline.o -o /lib/modules/`uname -r`/kernel/oss/osscore.ko
then
	echo Linking the osscore module failed
	exit 5
fi

#depmod -a

for n in ../modules/*.o
do
	N=`basename $n .o`
	echo Building module $N 

	rm -f $N_mainline.o Makefile

	sed "s/MODNAME/$N/" < Makefile.tmpl > Makefile
	ln -s $n $N_mainline.o

	if ! make KERNELDIR=$KERNELDIR > build.list 2>&1
	then
		echo Compiling module $N failed
		cat build.list
		exit 4
	fi

	if ! ld -r $N.ko $N_mainline.o -o /lib/modules/`uname -r`/kernel/oss/$N.ko
	then
		echo Linking $N module failed
		exit 6
	fi

	rm -f $N_mainline.o
	make clean
done 

rm -f Makefile

echo "depmod -a"
depmod -a

# Copy config files for any new driver modules

if ! test -d $OSSLIBDIR/conf
then
  mkdir $OSSLIBDIR/conf
fi

if test -d $OSSLIBDIR/conf.tmpl
then
  for n in $OSSLIBDIR/conf.tmpl/*.conf
  do
   N=`basename $n`
  
   if ! test -f $OSSLIBDIR/conf/$N
   then
     cp -f $n $OSSLIBDIR/conf/
   fi
  done
  rm -rf $OSSLIBDIR/conf.tmpl
fi

if ! test -f $OSSLIBDIR/etc/installed_drivers
then
   echo "-----------------------------"
   /usr/sbin/ossdetect -v
   echo "-----------------------------"
   echo 
fi

if ! test -d /etc/init.d
then
  mkdir /etc/init.d
fi

rm -f /etc/init.d/oss /etc/rc.d/rc3.d/S89oss /etc/rc3.d/S89oss
cp -f $OSSLIBDIR/etc/S89oss /etc/init.d/oss

chmod 744 /etc/init.d/oss

if test -x /sbin/chkconfig
then
  /sbin/chkconfig oss on        > /dev/null 2>&1
else
 if test -x /usr/sbin/update-rc.d
 then
   /usr/sbin/update-rc.d oss defaults > /dev/null 2>&1
 else
  if test -d etc/rc.d/rc3.d
  then
    rm -f /etc/rc.d/rc3.d/S89oss
    ln -s /etc/init.d/oss /etc/rc.d/rc3.d/S89oss
  else
    if test -d /etc/rc3.d
    then
      rm -f /etc/rc3.d/S89oss
      ln -s /etc/init.d/oss /etc/rc3.d/S89oss
    fi
  fi
 fi
fi

# Install ALSA interface module (Cuckoo)
#(cd $OSSLIBDIR/cuckoo && make clean) > /dev/null 2>&1
#if (cd $OSSLIBDIR/cuckoo && make install) > $OSSLIBDIR/logs/cuckoo.log 2>&1
#then
#  touch $OSSLIBDIR/.cuckoo_installed
#fi
#(cd $OSSLIBDIR/cuckoo && make clean) > /dev/null 2>&1

# Remove bogus char major 14 device files left from earlier OSS versions.

rm -f `ls -l -d /dev/*|grep ^c|grep '    14, '|sed 's/.* //'`

# Recompile libflashsupport.so if possible. Otherwise use the precompiled
# version.
(cd $OSSLIBDIR/lib;gcc -m32 -shared -fPIC -O2 -Wall -Werror -lssl flashsupport.c -o /usr/lib/libflashsupport.so) > /dev/null 2>&1

if test ! -f $OSSLIBDIR/etc/userdefs
then
  echo "autosave_mixer yes" > $OSSLIBDIR/etc/userdefs
fi

exit 0
