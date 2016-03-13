#!/bin/sh
rm -f os5
./xc -o os5 -Iroot/lib root/usr/os/os5.c
./xem os5
