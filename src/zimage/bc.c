/**************************** jfos booter's bc.c file **********************
 FD contains this booter in Sector 0, jfos kernel begins in Sector 1
 In the jfos kernel: word#1=tsize in clicks, word#2=dsize in bytes
****************************************************************************/
#include <sys/types.h>

int setup, ksectors, i;

readfd(int,int,int);
int prints(char *s)
{
    while (*s)
        putc(*s++);
}

int getsector(__u16 sector)
{
    readfd(sector / 36, ((sector) % 36) / 18, (((sector) % 36) % 18));
}

main()
{
    prints("booting jfos\n\r");
    setup = *(char *)(512+497); // number of SETUP sectors
    ksectors = *(int *)(512+500) >> 5; // number of kernel sectors
    setes(0x9000);
    for (i = 1; i <= setup+ksectors+2; i++)
    {
        getsector(i);
        i<= setup ? putc('*') : putc('.');
        inces();
        if(i==setup+1)
            setes(0x1000);
    }
    prints("\n\rready to go?");
    getc();
}
