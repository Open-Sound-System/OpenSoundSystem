#!/bin/sh
if test -f /etc/oss.conf
then
  . /etc/oss.conf
else
  OSSLIBDIR=/usr/lib/oss
fi

cd $OSSLIBDIR/build

rm -f $OSSLIBDIR/.cuckoo_installed

if ! test -f $OSSLIBDIR/objects/osscore.o
then
	echo Error: OSS core module is not available
	exit 1
fi

if ! test -f $OSSLIBDIR/modules/ich.o
then
	echo Error: OSS driver modules are not available
	exit 1
fi


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

if ! test -d /lib/modules/`uname -r`/kernel
then
	echo Error: Kernel directory /lib/modules/`uname -r`/kernel does not exist
	exit 1
fi

if ! test -d /lib/modules/`uname -r`/kernel/oss
then
	mkdir /lib/modules/`uname -r`/kernel/oss
fi



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

# Remove bogus char major 14 device files left from earlier OSS versions.

rm -f `ls -l -d /dev/*|grep ^c|grep '    14, '|sed 's/.* //'`

# Recompile libflashsupport.so if possible. Otherwise use the precompiled
# version.
(cd $OSSLIBDIR/lib;gcc -m32 -shared -fPIC -O2 -Wall -Werror -lssl flashsupport.c -o libflashsupport.so) > /dev/null 2>&1

exit 0
