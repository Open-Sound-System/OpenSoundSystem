#!/bin/sh
PROCS="`fuser /dev/mixer* /dev/dsp* /dev/audio* /dev/sequencer /dev/music /dev/midi*|sed 's/.* //'|sort|uniq`"

if test "$PROCS " = " "
then
   exit 0
fi

for pid in $PROCS
do
	ps ax|grep "^ *$pid "
	echo kill $pid
	kill $pid
done

sleep 2
exit 0
