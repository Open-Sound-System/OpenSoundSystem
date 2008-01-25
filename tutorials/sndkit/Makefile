#INSTALLDIR=/usr/local/bin
#INSTALLLIB=/usr/lib

$(INSTALLDIR)/stamp: ./stamp $(INCLUDEDIR)/soundcard.h
	 rm -f /usr/include/sys/soundcard.h
	 cp $(INCLUDEDIR)/soundcard.h /usr/include/sys/soundcard.h
	 make install

install:
	 cd OSSlib;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd ossmix;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 -cd ossxmix;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) install;make clean
	 -cd ossinfo;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) install;make clean
	 cd ossrecord;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd ossmplay;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd osstest;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd ossplay;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd awesfx;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 cd sblive;make INSTALLDIR=$(INSTALLDIR) INSTALLLIB=$(INSTALLLIB) CC=$(CC) CFLAGS=$(CFLAGS) install;make clean
	 rm -f $(INSTALLDIR)/stamp
	 -strip $(INSTALLDIR)/*
	 touch $(INSTALLDIR)/stamp

clean:
	rm -f core *.o x y z 
	cd OSSlib;make clean
	cd ossmplay;make clean
	cd ossplay;make clean
	cd ossrecord;make clean
	cd awesfx;make clean
	cd softsynth;make clean
	cd ossmix;make clean
	cd ossxmix;make clean
	cd ossinfo;make clean
	cd sblive;make clean
	cd osstest;make clean
