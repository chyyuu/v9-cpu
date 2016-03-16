# v9_cpu

Simple 32-bit v9-CPU with Simulator+Compiler+debugger+OS

## USAGE
### Prepare
in ubuntu 15.10 x86-64

```
sudo apt-get install gcc gdb make libc6-dev-i386
```

see boot-dbg.sh

### build c compiler

```
gcc -o xc -O3 -m32 -Ilinux -Iroot/lib root/bin/c.c
```
### buld v9-cpu simulator


```
gcc -o xem -O3 -m32 -Ilinux -Iroot/lib root/bin/em.c -lm
```

### build make-file-system tool

```
gcc -o xmkfs -O3 -m32 -Ilinux -Iroot/lib root/etc/mkfs.c
```

### build c compiler running in v9-cpu

```
./xc -o root/bin/c -Iroot/lib root/bin/c.c
```

### build os running in v9-cpu

```
./xc -o root/etc/os -Iroot/lib root/etc/os.c
```
### build debug info of os

```
./xc -s -Iroot/lib root/etc/os.c >os.dml 
```

### build file-system 

```
./xmkfs sfs.img root
mv sfs.img root/etc/.
./xmkfs fs.img root
cp ./root/etc/os .
```

### debug OR run os
debug(press 'h' for help)

```
./xem -g os.dml -f fs.img os
```

run

```
./xem  fs.img os
```
