/************************* algorithm of hd-booter's bc.c file ***************/
#define BOOTSEG 0x9800
#include "io.c"        // I/O functions such as printf()
#include "bootLinux.c" //  C code of Linux booter

#define SECTORS_PER_BLOCK 127

struct dap
{
    u8 len;       // dap length=0x10 (16 bytes)
    u8 zero;      // must be 0
    u16 nsector;  // actually u8; sectors to read=1 to 127
    u16 addr;     // memory address = (segment, addr)
    u16 segment;  // segment value
    u32 sectorLo; // low 4 bytes of LBA sector#
    u32 sectorHi; // high 4 bytes of LBA sector#
};

struct dap dap, *dp = &dap; // dap and dp are globals in C

int getSector(u32 sector, u16 offset)
{
    dp->nsector = 1;
    dp->addr = offset;
    dp->sectorLo = sector;
    diskr();
}

int getblk(u32 blk, u16 offset, u16 nblk)
{
    dp->nsector = nblk*SECTORS_PER_BLOCK; // max value=127
    dp->addr    = offset;
    dp->sectorLo = blk*SECTORS_PER_BLOCK;
    diskr();
}


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

int main()
{
    // 1. initialize dap INT13-42 calls
    dp->len = 0x10;             // dap length=0x10
    dp->zero = 0;               // this field must be 0
    dp->sectorHi = 0;           // assume 32-bit LBA, high 4-byte always 0
    // other fields will be set when the dap is used in actual calls

    struct GDT gdt;
    init_gdt(&gdt);
    cp2himem();
    

    // 2. read MBR sector

    // 3. print partition table

    // 4. prompt for a partition to boot

    // 5. if (partition type == LINUX) bootLinux(partition);

    // 6. load partition's local MBR to 0x07C0
    // chain-boot from partition's local MBR
}
