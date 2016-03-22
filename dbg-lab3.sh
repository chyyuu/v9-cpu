#!/bin/sh
./xc -o root/etc/os -Iroot/lib root/etc/os_lab3.c
./xc -s -Iroot/lib root/etc/os_lab3.c >os.dml 
cp ./root/etc/os .
./xem -g os.dml os
