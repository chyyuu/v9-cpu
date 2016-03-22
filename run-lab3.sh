#!/bin/sh
./xc -o root/etc/os -Iroot/lib root/etc/os_lab3.c
cp ./root/etc/os .
./xem os
