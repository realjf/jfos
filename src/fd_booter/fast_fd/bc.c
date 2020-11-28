
#define TRK 18
#define CYL 36
typedef unsigned char u8;
typedef unsigned short u16;
readfd(int, int, int);
getes();
int setup, ksectors, ES;
int csector = 1; // current loading sector
int NSEC = 35;   // initial number of sectors to load >= BOOT+SETUP
int getsector(u16 sector)
{
    readfd(sector / CYL, ((sector) % CYL) / TRK, (((sector) % CYL) % TRK));
    csector += NSEC;
    inces();
}

main()
{
    setes(0x9000);
    getsector(1); // load linux's [boot+SETUP] to 0x9000
    // current sector= SETUP's sector count (at offset 512+497) + 2
    setup = *(u8 *)(512 + 497) + 2;
    ksectors = (*(u16 *)(512 + 500)) >> 5;
    NSEC = CYL - setup; // sectors remain in cylinder 0
    setes(0x1000);      // linux kernel is loaded to segment 0x1000
    getsector(setup);   // load the remaining sectors of cylinder 0
    csector = CYL;      // we are now at begining of cyl#1
    while (csector < ksectors + setup)
    {
        // try to load cylinders
        ES = getes(); // current ES value
        if (((ES + CYL * 0x20) & 0xF000) == (ES & 0XF000))
        {
            // same segment
            NSEC = CYL; // load a full cylinder
            getsector(csector);
            putc('C'); // show loaded a cylinder
            continue;
        }
        // this cylinder will cross 64KB, compute MAX sectors to load
        NSEC = 1;
        while (((ES + NSEC * 0x20) & 0xF000) == (ES & 0xF000))
        {
            NSEC++;
            putc('s'); // number of sectors can still load
        }
        getsector(csector); // load partial cylinder
        NSEC = CYL - NSEC;  // load remaining sectors of cylinder
        putc('|');          // show cross 64KB
        getsector(csector); // load remainder of cylinder
        putc('p');
    }
}
