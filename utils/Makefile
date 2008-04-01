CFLAGS=-I../include

PROGRAMS=snoopy mixgen readtimings

all: $(PROGRAMS)

snoopy: snoopy.c ../kernel/drv/hdaudio/hdaudio.h
	$(CC) -o snoopy snoopy.c -I../kernel/drv/hdaudio $(CFLAGS)

mixgen: mixgen.c ../kernel/drv/hdaudio/hdaudio.h
	$(CC) -o mixgen mixgen.c -I../kernel/drv/hdaudio $(CFLAGS)

mixgen2: mixgen2.c ../kernel/drv/hdaudio/hdaudio.h
	$(CC) -o mixgen2 mixgen2.c -I../kernel/drv/hdaudio $(CFLAGS)

clean:
	rm -f $(PROGRAMS) core core.* *.core *~ x y z *.o
