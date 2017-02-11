#!/bin/sh
rm -f tools/xc tools/xem tools/dis os_printchar
#build compiler:xc, simulator: xem, dissambler:dis
gcc -o tools/xc -O3 -m32 tools/c.c
gcc -o tools/xem -O3 -m32 tools/em.c -lm
gcc -o tools/dis -O3 tools/dis.c
# compile os_printchar
./tools/xc -o os_printchar os_printchar.c
# dissamble os_printchar
./tools/dis os_printchar -o os_printchar.s -c
# run os_printchar in simulator:xem
./tools/xem os_printchar
