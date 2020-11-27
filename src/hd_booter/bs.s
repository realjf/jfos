! ==================== hd=booter's bs.s file ==============================
BOOSEG  = 0x9800
SSP     = 32 * 1024         ! 32KB bss + stack; my be adjusted
.globl  _main, _prints, dap,_dp,_bsector,_vm_gdt        ! IMPORT
.globl  _diskr,_getc,_putc,_getds,_setds                ! EXPORT
.globl  _cp2himem,_jmp_setup,_gdt
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


