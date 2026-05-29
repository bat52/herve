/** firmware_blinky.c - Blinky LED demo for Herve RISC-V ISS + Verilator */
#define GPIO_OUT     (*(volatile unsigned int*)0x10000000)
#define MTIME_LO     (*(volatile unsigned int*)0x10000010)
#define MTIMECMP_LO  (*(volatile unsigned int*)0x10000018)
#define MTIMECMP_HI  (*(volatile unsigned int*)0x1000001C)
#define INTERVAL     50000
static unsigned int next;
extern void _vector_table(void);
static void wfi(void) { __asm__ volatile("wfi"); }
void interrupt_handler(void) {
    unsigned int c; __asm__ volatile("csrr %0,mcause":"=r"(c));
    if ((c&0x1F)==7) { GPIO_OUT ^= 1; next += INTERVAL; MTIMECMP_LO = next; }
}
__asm__(".globl _vector_table\n.type _vector_table,@function\n_vector_table:\n"
"addi sp,sp,-16\nsw ra,0(sp)\nsw a0,4(sp)\nsw a1,8(sp)\nsw a2,12(sp)\n"
"jal ra,interrupt_handler\n"
"lw ra,0(sp)\nlw a0,4(sp)\nlw a1,8(sp)\nlw a2,12(sp)\naddi sp,sp,16\nmret\n");
void _start(void) {
    __asm__("la sp,_stack_top");
    __asm__("csrw mtvec,%0"::"r"((unsigned int)&_vector_table));
    next = MTIME_LO + INTERVAL; MTIMECMP_LO = next; MTIMECMP_HI = 0;
    __asm__("csrw mie,%0"::"r"(0x80u));
    __asm__("csrs mstatus,%0"::"r"(0x8u));
    while(1) wfi();
}
