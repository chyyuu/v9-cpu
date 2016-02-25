#!/bin/sh
rm -f xc xem funcall funcall.txt
gcc -o xc -O3 -m32 -Ilinux -Iroot/lib root/bin/c.c
gcc -o xem -O3 -m32 -Ilinux -Iroot/lib root/bin/em.c -lm
gcc -o dis -O3 root/bin/dis.c
./xc -o funcall -Iroot/lib root/usr/funcall.c
./xc -s -Iroot/lib root/usr/funcall.c >funcall.txt
./dis funcall -o funcall.s -c
./xem funcall

