#!/bin/sh
rm -f xc xem dis emhello funcall os0 os1 os2 os3 os4
gcc -o xc -O3 -m32 -Ilinux -Iroot/lib root/bin/c.c
gcc -o xem -O3 -m32 -Ilinux -Iroot/lib root/bin/em.c -lm
gcc -o dis -O3 root/bin/dis.c
./xc -o emhello -Iroot/lib root/usr/emhello.c
./xc -o funcall -Iroot/lib root/usr/funcall.c
./xc -o os0 -Iroot/lib root/usr/os/os0.c
./xc -o os1 -Iroot/lib root/usr/os/os1.c
./xc -o os2 -Iroot/lib root/usr/os/os2.c
./xc -o os3 -Iroot/lib root/usr/os/os3.c
./xc -o os4 -Iroot/lib root/usr/os/os4.c
./dis os1 -o os1.s -c
./dis os2 -o os2.s -c
./dis os3 -o os3.s -c
./dis os4 -o os4.s -c
