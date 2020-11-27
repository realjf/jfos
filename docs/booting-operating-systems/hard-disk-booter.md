## 硬盘引导程序
本节，我们将要为从硬盘分区引导jfos和bzImage镜像开发一个在线引导程序。

HD引导程序由五个文件组成：

- bs.s 汇编代码
- bc.c C代码，包括io.c，bootJfos.c和bootLinux.c

在引导期间，会展示硬盘分区和提示引导分区号。如果分区类型是jfos(90)或者linux(83)，
允许用户输入一个文件名进行启动。如果用户仅输入回车键，将启动/boot/jfos或者/boot/vmlinuz作为默认启动。
当启动linux时，也会支持一个RAM磁盘镜像作为最开始。对于非jfos或linux分区，将作为一个引导链去引导其他操作系统，
如：windows。从引导jfos开始都非常简单，我们应该只是讨论linux部分的HD引导程序。

HD引导程序包含5个逻辑部分。每个部分是完全独立的编程任务，即能分开处理。例如，我们可以写一个C程序展示
硬盘分区，并在linux下优先测试它。相似的，我们可以写一个程序，在EXT2文件系统中找一个文件的inode，并打印它的磁盘blocks。
当程序被测试正常工作时，我们调整它们以使其作为16位环境引导程序的一部分。以下描述了HD引导程序的逻辑组件。

### I/O和内存访问函数
一个HD引导程序不在被限制在512字节或1024字节。对于一个大尺寸代码，我们应该实现一个I/O函数集合，以便在引导期间提供更好的用户接口。
特别地，我们应该实现一个gets()函数，其允许用户输入可引导镜像文件名和引导参数，而一个printf()函数将提供格式化打印。

首先，我们先看看gets()函数：

```
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

```

对于输出，我们首先实现一个printu()函数，其打印无符号short整型

```c
typedef unsigned short u16;
typedef unsigned long u32;
char *ctable = "0123456789ABCDEF";
u16 BASE = 10; // for decimal numbers
int rpu(u16 x)
{
    char c;
    if(x){
        c = ctable[x%BASE];
        rpu(x/BASE);
        putc(c);
    }
}

int printu(u16 x)
{
    (x==0) ? putc('0') : rpu(x);
    putc(' ');
}
```
rpu(x)函数递归生成ASCII表中 x%/10 的数字，并在返回路径上打印它们。例如，如果x=123，
将按顺序生成'3','2','1'的ASCII数字，即被打印'1','2','3'。对于printu()，写一个printd()去打印有符号short整型变成微不足道。

通过设置BASE为16，我们可以打印16进制，通过改变参数类型为__u32，我们能打印long型值，如：LBA磁盘扇区和inode编号。假设我们有prints()，
printd(),printu(),printx(),printl()和printX()，而printl()和printX()分别打印32位十进制和十六进制值。然后写一个printf(char* fmt, ...)格式化打印，
其中fmt是包含转化符号%c，%s,%u,%d,%x,%l,%X的格式化字符串。

```c
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
        case 'l': printl(*(u32 *) ip++); break;
        case 'X': printX(*(u32 *)ip++); break;
        }
        cp++;
        ip++;
    }
}
```
简单printf()函数不能支持域宽或精度，但是他足以在启动期间执行打印任务。它将极大改善引导程序代码的可读能力。
同样printf()函数也会被用于之后的jfos内核中。

当启动linux bzImage镜像时，引导程序必须获取SETUP扇区的编号以便决定如何加载镜像的剩余部分。在加载完镜像后，
它必须设置已加载的启动参数BOOT和SETUP的扇区数以便linux内核使用。为了这么做，我们实现了get_byte()/put_byte()函数，
其非常相似于传统的peek()/poke()函数。

```c
typedef unsigned char u8;
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
```
相似地，我们可以实现get_word()/put_word()作为读取/写入 2字节 word。
这些函数允许引导程序访问它自己段之外的内存。

### 读取硬盘LBA扇区

不像软盘，使用CHS寻址，大硬盘使用线性块寻址（Linear Block Addressing,LBA），在磁盘里扇区是通过32位或48位扇区编号线性访问的。
为了使用LBA读取硬盘扇区，我们可以用扩展BIOS INT13-42(INT 0X13,AH=0x42)函数，INT32-42的参数是被磁盘地址包(Disk Address Packet,DAP)结构指定的。

```c
struct dap{
    u8  len;    // dap length=0x10 (16 bytes)
    u8  zero;   // must be 0
    u16 nsector;    // actually u8; sectors to read=1 to 127
    u16 addr;   // memory address = (segment, addr)
    u16 segment;    // segment value
    u32 sectorLo;   // low 4 bytes of LBA sector#
    u32 sectorHi;   // high 4 bytes of LBA sector#
};
```

为了调用INT 13-42，我们定义一个全局dap结构并初始化它，如下：
```c
struct dap dap, *dp=&dap; // dap and dp are globals in C
dp->len = 0x10; // dap length=0x10
dp->zero = 0; // this field must be 0
dp->sectorHi= 0;    // assume 32-bit LBA, high 4-byte always 0
// other fields will be set when the dap is used in actual calls
```

在上述C代码中，我们可以设置dap的段，然后调用getSector()去加载一个磁盘扇区到内存定位(segment,offset)中，如下：
```c
int getSector(u32 sector, u16 offset)
{
    dp->nsector = 1;
    dp->addr = offset;
    dp->sectorLo = sector;
    diskr();
}
```
diskr()在汇编代码中，用于在全局dap中调用BIOS INT 13-42中断。

```asm
.globl _diskr,_dap ! _dap is a global dap struct in C
_diskr:
    mov     dx, #0x0080     ! device=first hard drive
    mov     ax, #0x4200     ! AH=0x42
    mov     si, #_dap       ! (ES,SI) points to _dap
    int     0x13            ! call BIOS INT13-42 to read sectors
    jb      _error          ! to error() if CarryBit is set (read failed)
    ret
```
相似地，这个函数：

```c
int getblk(u32 blk, u16 offset, u16 nblk)
{
    dp->nsectors = nblk*SECTORS_PER_BLOCK; // max value=127
    dp->addr    = offset;
    dp->sectorLo = blk*SECTORS_PER_BLOCK;
    diskr();
}
```
加载从(segment,offset)开始的nblk相邻的磁盘块到内存中，其nblk <= 15 因为 dp->nsectors<=127

### 使用初始化Ramdisk镜像启动Linux bzImage
当启动linux bzImage时，镜像的BOOT+SETUP被加载到0X9000与之前一样，但是linux内核被加载到物理地址的高地址0x100000(1MB)上。
如果一个RAMdisk镜像被指定，它也被加载到高内存地址，在16位真实模式引导之后，它将不能直接访问1MBy以上的内存。虽然我们可以切换
PC机到保护模式，以访问高内存，并且在那之后切换回真实模式，为了做到这个需要做很多工作。一个更好的方式是用BIOS INT15-87中断，
其被设计为在真实模式和保护模式间复制内存。INT15-87的参数在一个全局描述表（Global Descriptor Table, GDT）中指定。

```c
struct GDT
{
    u32 zeros[4];   //  16 bytes 0's for BIOS to use
    // src address
    u16 src_seg_limit;  // 0xFFFF = 64KB
    u32 src_addr;   // low 3 bytes of src addr, high_byte =0x93
    u16 src_hiword; // 0x93 and high byte of 32-bit src addr 
    // dest address
    u16 dest_seg_limit; // 0xFFFF = 64KB
    u32 dest_addr;      // low 3 bytes of dest addr, high byte=0x93
    u16 dest_hiword;    // 0x93 and high byte of 32-bit dest addr
    // BIOS CS DS
    u32 bzeros[4];
};
```
GDT指定一个src地址和一个dest地址，两个都是32位物理地址。然而，构成这些地址的字节不是相邻的，这使得
他们很难接近。虽然src_addr和dest_addr两个都是被定义为u32，但是低3字节地址的一部分，即高字节访问权限是被设置为0x93.
相似地，src_hiword和dest_hiword两个也别定义为u16,但只有高字节才是第4个地址字节，其低字节访问权限也是被设置为0x93.

例如，如果我们想从真实地址 0x00010000(64KB) 复制到 0x01000000(16 MB)的内存，GDT将作如下初始化：

```c
init_gdt(struct GDT *p)
{
    int i;
    for(i=0; i<4; i++)
        p->zerors[i] = p->bzeros[i] = 0;
    p->src_seg_limit = p->dest_seg_limit = 0xFFFF; // 64KB segments
    p->src_addr = 0x93010000;   // bytes 0x00 00 01 93
    p->dest_addr = 0x93000000;  // bytes 0x00 00 00 93
    p->src_hiword = 0x0093;     // bytes 0x93 00
    p->dest_hiword = 0x0193;    // bytes 0x93 01
}
```
以下代码段从0x00010000(64KB)的真实模式内存中复制4096字节到0x01000000(16MB)高内存中：

```c
struct GDT gdt; // define a gdt struct
init_gdt(&gdt); // initialize gdt as shown above
cp2himem(); // assembly code that does the copying
```

```asm
.globl _cp2himem,_gdt       ! _gdt is a global GDT from C
_cp2himem:
    mov     cx, #2048       ! CX=number of 2-byte words to copy
    mov     si, #_gdt       ! (ES,SI) point to GDT struct
    mov     ax, #0x8700     ! AH=0x87
    int     0x15            ! call BIOS INT15-87
    jc      _error
    ret
```
基于以上，我们可以加在一个镜像文件的块到高内存：

1. 加在一个磁盘块(4KB或8个扇区)到段0x1000;
2. cp2himem();
3. gdt.vm_addr+=4096;
4. 重复步骤1·3加载下一个块等

这能作为引导程序的基本加载方案被使用，为了快速加载，硬盘引导程序尽量一次性加载达到15个相邻块。这是很显然的，大多数PC支持
一次性加载16个相邻块。在这些机器上，镜像能被64KB分片加载。



### 硬盘分区
硬盘分区表是在MBR扇区中的446(0x1BE)偏移字节中。分区表有4个入口，每个通过一个16字节分区结构进行定义，即：
```c
struct partition {
    u8 drive;   // 0x80 - active
    u8 head;    // starting head
    u8 sector;  // starting sector
    u8 cylinder;    // starting cylinder
    u8 sys_type;    // partition type
    u8 end_head;    // end head
    u8 end_sector;  // end sector
    u8 end_cylinder;    // end cylinder
    u32 start_sector;   // starting sector counting from 0
    u32 nr_sectors;     // number of sectors in partition
};
```
如果一个分区是扩展类型(5)，它能被分割为更多分区。假设分区P4是扩展类型，且它被分割为
扩展分区P5,P6,P7，扩展分区通过一个链表组织，如下：

![link-list-of-extended-partition.png](/images/link-list-of-extended-partition.png)

一个仅包含两个入口的分区表，第一个入口定义了开始扇区的编号和扩展分区的大小。第二个入口指向下一个本地MBR。
所有本地MBR的扇区编号都是相对于P4的开始扇区。通常，该链表最后一个本地MBR是以一个0结尾的，在一个分区表中，
CHS值仅对于小于8GB的地址有效，对于大于8GB的但小于4G扇区的磁盘，只有最后两个入口start_sector和nr_sectors是有意义的。
因此，引导程序应该仅展示类型，开始扇区和分区大小。



### 寻找并加载linux内核并初始化镜像文件





### linux和jfos硬盘引导程序


### 启动EXT4分区


### 安装硬盘引导程序




