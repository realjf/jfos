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
    dp->nsector = nblk*SECTORS_PER_BLOCK; // max value=127
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
        p->zeros[i] = p->bzeros[i] = 0;
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
用于查找Linux bzImage或RAM磁盘映像的步骤基本上是相同的
和以前一样。主要的区别源于需要遍历大型EXT2/EXT3硬盘上的文件系统

- 在一个硬盘分区里，EXT2/EXT3文件系统的超级块是1024字节偏移位置。一个启动程序必须读取超级块以便获取s_first_data_block,
s_log_block_size,s_inodes_per_group和s_inode_size的值，其s_log_block_size决定了块大小，而块大小又决定了group__desc_per_block的值，
inodes_per_block的值等。这些值在遍历文件系统时是需要的

- 一个大的EXT2/EXT3文件系统可能有很多组，组描述器在块的开始（1+s_first_data_block），通常是1，给一个组编号，我们必须找到他的组描述器并
用它找到组的inodes开始块

- 核心问题是如何转换一个inode编号为一个inode，下面的代码段说明了该算法，相当于应用了Mailman的算法两次

```
# a. Compute group# and offset# in that group
group = (ino-1)/inodes_per_group;
inumber = (ino-1)%inodes_per_group;

# b. find the group's group descriptor
gdblk = group / desc_per_block; // which block this GD is in
gdisp = group % desc_per_block; //which GD in that block

# c. Compute inode's block# and offset in that group
blk=inumber / inodes_per_block; // blk# r.e.to group inode_table
disp = inumber % inodes_per_block; // inode offset in that block

# d. read group descriptor to get group's inode table start block#
getblk(1+first_data_block+gdblk, buf, 1); // GD begin block
gp = (GD*)buf + gdisp; // it's this group desc
blk += gp->bg_inode_table; // blk is r.e. to group's inode_table
getblk(blk, buf, 1); // read the disk block containing inode
INODE *ip =(INODE*)buf + (disp*iratio); // iratio=2 if inode_size=256

```
当算法结束，INODE *ip 应该指向该文件的inode内存地址

- 加载linux内核和ramdisk镜像到高内存：通过getblk()和cp2himem()，直接加载内核镜像到1MB的高内存。
这唯一复杂的是当内核镜像不是从块边界开始的时候。例如，如果SETUP扇区的数目是12，那么内核的5个扇区在
块1，必须先加载到0x100000，然后才能加载其余的块block1，其必须在我们能逐块加载剩余内核之前，被首先加载到0x100000。
相反，如果SETUP扇区的数目是23，则BOOT和SETUP在前3个块中，内核从块3开始。在这种情况下，我们可以按块加载整个内核，而不必处理
开始块的分数。虽然硬盘引导程序可以正确地处理这些情况，但它确实是一种痛苦。如果每个bzImage的Linux内核从一个块边界开始将更好了。
这可以很容易地通过在Linux工具程序中修改几行代码来实现将不同的部件组装成bzImage文件。为什么Linux
人们除我之外选择不这样做

接下啦，我们考虑加载RAMdisk镜像。Slackware（Slackware Linux）也有初始化HOWTO文件。initrd是一个由Linux使用的小文件系统，
内核启动时作为临时根文件系统。initrd包含一组最小的目录和可执行文件，如sh、ismod工具和需要的驱动模块。在initrd上运行时，
Linux内核通常执行一个sh脚本initrc，用于安装所需的驱动程序模块并激活真正的root设备。当真正的root设备准备就绪时，Linux内核将放弃initrd，
并装载真正的根文件系统以完成两阶段启动过程。
使用initrd方法的原因如下：在引导过程中，Linux的启动代码只激活一些标准设备，比如FD和IDE/SCSI HD作为可能的root设备。
其他设备驱动程序要么以后作为模块安装，要么根本不激活。这个即使所有设备驱动程序都内置在Linux内核中，也是正确的。尽管
可以通过改变内核的启动代码激活所需的root设备，但问题是，有这么多不同的Linux系统配置，哪种设备激活？
一个显而易见的答案是激活它们。这样的Linux内核应该是巨大的体积和相当慢的启动。例如，在一些Linux发行包中，内核映像大于4mb。
initrd镜像可以是根据说明量身定制，仅安装所需的驱动模块。这允许一个通用的Linux内核可用于各种Linux系统配置。
理论上，一般的Linux内核只需要RAM磁盘驱动程序就可以启动。所有其他驱动程序可以作为模块从initrd安装。有很多工具可以创建初始映像。
Linux中的mkinitrd命令就是一个很好的例子。它创造了一个initrdgz文件以及包含initrd文件系统的initrd树目录。如果根据需要，
可以修改initrd树以生成新的initrd镜像。旧的initrd.gz镜像是经过压缩的EXT2文件系统，可以解压缩并作为循环文件系统装载。
更新的initrd镜像是cpio存档文件，它可以被cpio实用程序操纵。假设初始镜像是一个RAM磁盘镜像文件。
首先，将其重命名为initrd.gz，然后运行gunzip解压。然后运行：

```sh
mkdir temp; cd temp; # use a temp DIR
cpio -id < ../initrd # extract initrd contents
```
提取内容，在测试之后，修改initrd树里的文件，运行：
```sh
find .|cpio -o -H newc | gzip > ../in
```
以创建一个新的initrd.gz文件

加载initrd镜像类似于加载内核镜像，只是更简单。对initrd的加载地址没有具体要求，除了最大地址上限为0xFE000000。
除此限制外，任何合理的装载地址似乎都是有效的，好的。硬盘引导程序将Linux内核加载到1MB，initrd加载到32MB。加载完成后，
引导程序必须分别在字节偏移量24和28处写入initrd镜像的加载地址和大小。然后跳转到0x9020执行SETUP。
早期SETUP代码不关心段寄存器设置。在内核2.6中，SETUP程序需要DS=0x9000才能访问BOOT作为数据段的开始。


### linux和jfos硬盘引导程序
一个完整的硬盘引导程序代码列表在BOOTERS/HD/MBR.ext4/中，引导程序能启动jfos和ramdisk初始化的linux，它也能启动
windows通过启动链。为了简洁起见，我们仅展示启动linux部分。

```asm
! ==================== hd=booter's bs.s file ==============================
BOOSEG  = 0x9800
SSP     = 32 * 1024         ! 32KB bss + stack; my be adjusted
.globl  _main, _prints, dap,_dp,_bsector,_vm_gdt        ! IMPORT
.globl  _diskr,_getc,_putc,_getds,_setds                ! EXPORT
.globl  _cp2himem,_jmp_setup
! MBR loaded at 0x07C0. Load entire booter to 0x9800
start:  
    mov     ax, #BOOTSEG
    mov     es, ax
    xor     bx, bx              ! clear BX=0
    mov     dx, #0x0080         ! head 0, HD
    xor     cx, cx              
    incb    cl                  ! cyl 0, sector 1
    incb    cl                  
    mov     ax, #0x0220         ! READ 32 sectors, booter size up to 16KB
    int     0x13
! far jump to (0x9800, next) to continue execution there
    jmpi    next, BOOSEG        ! CS=BOOTSEG, IP=next
next:
    mov     ax, cs              ! set CPU segment registers
    mov     ds, ax              ! we know ES,CS=0x9800. Let DS=CS
    mov     ss, ax  
    mov     es, ax              ! CS=DS=SS=ES=0x9800
    mov     sp, #SSP            ! 32KB stack
    call    _main               ! call main() in C
    test    ax, ax              ! check return value from main()
    je      error               ! main() return 0 if error
    jmpi    0x7C00, 0x0000      ! otherwise, as a chain booter
_diskr:
    mov     dx, #0x0080         ! drive=0x80 for HD
    mov     ax, #0x4200         
    mov     si, #_dap
    int     0x13                ! call BIOS INT13-42 read the block
    jb      error               ! to error if CarryBit is on
    ret
error:
    mov     bx, #bad
    push    bx
    call    _prints
    int     0x19                ! reboot
bad:    .asciz "\n\rError!\n\r"
_jmp_setup:
    mov     ax, 0x9000          ! for SETUP in 2.6 kernel:
    mov     ds, ax              ! DS must point at 0x9000
    jmpi    0, 0x9020           ! jmpi to execute SETUP at 0x9020

!============================ I/O functions ==============================
_getc: ! char getc(): return an input char
    xorb    ah, ah          ! clear ah
    int     0x16            ! call BIOS to get char in AX 
    ret
_putc: ! putc(char c): print a char
    push    bp
    mov     bp, sp
    movb    al, 4[bp]       ! aL = char
    movb    ah, #14         ! aH = 14
    int     0x10            ! call BIOS to display the char
    pop     bp
    ret
_setds:                     ! setes(segment): set DS to a segment
    push    bp
    mov     bp, sp
    mov     ax, 4[bp]
    mov     ds, ax
    pop     bp
    ret
_getds:
    xor     ax, ax          ! AX=0
    mov     ax, ds          ! current DS value
    ret

!----------------------- cp2himem() -------------------------------
! for each batch of k<=16 blocks, load to RM=0x10000 (at most 64KB)
! then call cp2himem() to copy it to      VM=0x100000 + k*4096
!------------------------------------------------------------------
_cp2himem:
    push    bp
    mov     bp, sp
    mov     cx, 4[bp]       ! words to copy (32*1024 or less)
    mov     si, #_vm_gdt
    mov     ax, #0x8700
    int     0x15
    jc      error
    pop     bp
    ret

```

```c
#define BOOTSEG 0x9800
#include "io.c"
#include "bootLinux.c"
int main()
{
    // 1. initialize dap INT13-42 calls

    // 2. read MBR sector

    // 3. print partition table

    // 4. prompt for a partition to boot

    // 5. if (partition type == LINUX) bootLinux(partition);

    // 6. load partition's local MBR to 0x07C0
    // chain-boot from partition's local MBR
}

```

```c

```

如上算法，步骤8是可选的。步骤9设置root设备，这仅在没有初始化镜像被加载时才需要。对于一个初始化镜像，root设备
通过初始化镜像决定。步骤（10）是必需的，它告诉SETUP程序
内核映像由“up-to-date”引导加载程序加载。否则,SETUP代码
会认为加载的内核映像无效并拒绝启动Linux内核。

### 启动EXT4分区


### 安装硬盘引导程序




