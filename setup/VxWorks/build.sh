#!/bin/sh

mkdir prototype
mkdir prototype/oss-install

ld -r -o prototype/oss-install/oss.o target/objects/*.o

exit 0
