#!/bin/sh
rm -f xc xem dis funcall funcall.txt funcall.s root/bin/c  root/etc/os fs.img
gcc -o xc -O3 -m32 -Ilinux -Iroot/lib root/bin/c.c
gcc -o xem -O3 -m32 -Ilinux -Iroot/lib root/bin/em.c -lm
gcc -o dis -O3 root/bin/dis.c
gcc -o xmkfs -O3 -m32 -Ilinux -Iroot/lib root/etc/mkfs.c
./xc -o root/bin/c -Iroot/lib root/bin/c.c
./xc -o root/etc/os -Iroot/lib root/usr/os/os.c
./xmkfs fs.img root
./xem  -f fs.img root/etc/os
