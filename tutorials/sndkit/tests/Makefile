TARGETS=iosync multich16 multich32 musicin seltest2 spdif_in_debug sweepdown sweepup synctest

all: ${TARGETS}

sweepup:
	$(CC) $(LDFLAGS) -lm -o sweepup sweepup.c 

clean:
	rm -f ${TARGETS} *.o
