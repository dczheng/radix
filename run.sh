#!/bin/sh

make &&
kldload ./test_radix.ko &&
kldunload test_radix.ko &&
dmesg -c

make clean 2>&1 > /dev/null
