#!/bin/sh
rm -f xc xem os5
gcc -o xc -O3 -m32 -Ilinux -Iroot/lib root/bin/c.c
gcc -o xem -g -m32 -Ilinux -Iroot/lib root/bin/em.c -lm
./xc -o os5 -Iroot/lib root/usr/os/os5.c
./xem os5
