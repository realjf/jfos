# usage: mk filename
as86 -o bs.o bs.s # bs.s file does not change
bcc -c -ansi bc.c
ld86 -d -o booter bs.o bc.o /usr/lib/bcc/libc.a
dd if=booter of=booter.img bs=512 count=1 conv=notrunc

# booting from floppy
qemu-system-i386 -fda ./booter.img -no-fd-bootchk