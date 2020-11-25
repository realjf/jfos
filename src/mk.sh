# usage: mk filename
as86 -o bs.o bs.s # bs.s file does not change
bcc -c -ansi bc.c
ld86 -d -o booter bs.o bc.o /usr/lib/bcc/libc.a
dd if=booter of=/dev/fd0 bs=512 count=1 conv=notrunc

# booting from floppy
qemu-system-i386 -drive file=/dev/fd0,index=0,if=floppy -no-fd-bootchk