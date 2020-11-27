

#define MAXLEN 128
char *gets(char s[]) // caller must provide REAL memory s[MAXLEN]
{
    char c, *t = s;
    int len = 0;
    while((c=getc()) != '\r' && len < MAXLEN-1){
        *t++ = c;
        putc(c);
        len++;
    }
    *t = 0;
    return s;
}

int prints(char *s)
{
    while (*s)
        putc(*s++);
}

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
char *ctable = "0123456789ABCDEF";

int rpu(u16 x, u16 BASE)
{
    char c;
    if(x){
        c = ctable[x%BASE];
        rpu(x/BASE, BASE);
        putc(c);
    }
}

int printu(u16 x)
{
    (x==0) ? putc('0') : rpu(x, 10);
    putc(' ');
}

int printd(int x){
	if (x < 0){
		x = -x;
		prints("-");
	}
	(x==0)? putc('0'):
	printu(x);
}

//printl
int printl(u32 x){
	(x==0 )? putc('0') : rpu(x,32);
	putc(' ');
}

//printx
int printx(u16 x){
	prints("0x");
	(x==0)? putc('0') : rpu(x, 16);
	putc(' ');
}

int printX(u32 x){
	prints("0X");
	(x==0) ? putc('0') : rpu(x, 32);
	putc(' ');
}

int printf(char *fmt, ...) // some C compiler requires the three dots
{
    char *cp = fmt; // cp points to the fmt string
    u16 *ip = (u16*)&fmt + 1; // ip points to first item
    u32 *up;    // for accessing long parameters on stack
    while(*cp) { // scan the format string
        if(*cp != '%'){ // spit out ordinary chars
            putc(*cp);
            if(*cp == '\n') // for each '\n'
                putc('\r');
            cp++;
            continue;
        }
        cp++;   // print item by %FORMAT symbol
        switch (*cp)
        {
        case 'c': putc(*ip); break;
        case 's': prints(*ip); break;
        case 'u': printu(*ip); break;
        case 'd': printd(*ip); break;
        case 'x': printx(*ip); break;
        case 'l': printl(*(u32 *)ip++); break;
        case 'X': printX(*(u32 *)ip++); break;
        }
        cp++;
        ip++;
    }
}

u8 get_byte(u16 segment, u16 offset)
{
    u8 byte;
    u16 ds = getds(); // getds() 在汇编代码中返回DS的值
    setds(segment); // set DS to segment
    byte = *(u8 *)offset;
    setds(ds);
    return byte;
}

void put_byte(u8 byte, u16 segment, u16 offset)
{
    u16 ds = getds(); //save DS
    setds(segment); // set DS to segment
    *(u8 *)offset = byte;
    setds(ds); // restore DS
}

