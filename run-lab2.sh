#!/bin/sh
./xc -o root/etc/os -Iroot/lib root/etc/os_lab2.c
cp ./root/etc/os .
./xem os
