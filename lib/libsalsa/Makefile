LOBJECTS=main.lo output.lo misc.lo pcm.lo mix.lo seq.lo seqmid.lo rawmidi.lo seq_output.lo timer.lo stubs.lo seq_input.lo

OSSLIBDIR=/usr/lib/oss

CFLAGS=-I../../include -I../libOSSlib

all: libsalsa.la

*.lo:		local.h 

main.lo:	main.c
		libtool --mode=compile gcc $(CFLAGS) -c main.c

pcm.lo:	pcm.c
		libtool --mode=compile gcc $(CFLAGS) -c pcm.c 

misc.lo:	misc.c
		libtool --mode=compile gcc $(CFLAGS) -c misc.c

stubs.lo:	stubs.c
		libtool --mode=compile gcc $(CFLAGS) -c stubs.c

mix.lo:	mix.c
		libtool --mode=compile gcc $(CFLAGS) -c mix.c

output.lo:	output.c
		libtool --mode=compile gcc $(CFLAGS) -c output.c

seq.lo:	seq.c
		libtool --mode=compile gcc $(CFLAGS) -c seq.c 

timer.lo:	timer.c
		libtool --mode=compile gcc $(CFLAGS) -c timer.c 

seqmid.lo:	seqmid.c
		libtool --mode=compile gcc $(CFLAGS) -c seqmid.c 

seq_input.lo:	seq_input.c
		libtool --mode=compile gcc $(CFLAGS) -c seq_input.c 

seq_output.lo:	seq_output.c
		libtool --mode=compile gcc $(CFLAGS) -c seq_output.c 

rawmidi.lo:	rawmidi.c
		libtool --mode=compile gcc $(CFLAGS) -c rawmidi.c 

libsalsa.la:   $(LOBJECTS)
		libtool --mode=link gcc -rpath /usr/lib -Wl,--version-script=Versions -version-info 2:0:0 -o libsalsa.la $(LOBJECTS) -L../libOSSlib -lOSSlib

install:	libsalsa.la
		libtool cp libsalsa.la /usr/lib
		(cd /usr/lib;ln -sf libsalsa.so.2.0.0 libasound.so)
		(cd /usr/lib;ln -sf libsalsa.so.2.0.0 libasound.so.2)

dep:

clean:
		rm -f core core.* *.o *.a *.lo *.so *.loT *.la x y z *.s
		rm -rf .libs
