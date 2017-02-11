#!/bin/sh
# clean tools and os
rm -f tools/xc tools/xem tools/dis os_helloworld
#build compiler:xc, simulator: xem, dissambler:dis
gcc -o tools/xc -O3 -m32 tools/c.c
gcc -o tools/xem -O3 -m32 tools/em.c -lm
gcc -o tools/dis -O3 tools/dis.c
# compile os_printchar
./tools/xc -o os_helloworld os_helloworld.c
# dissamble os_printchar
./tools/dis os_helloworld -o os_helloworld.s -c
# run os_printchar in simulator:xem
echo Build OS Done!
