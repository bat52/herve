/**
 * rv32_dpi_elf_test.c — Quick test of the ELF loader.
 *
 * Compiles a tiny RISC-V program, links it as an ELF, then loads it
 * via rv_init_elf() and executes it to verify the loader works.
 *
 * Compile:
 *   g++ -I. -o rv32_dpi_elf_test rv32_dpi_elf_test.c rv32_dpi.c
 *
 * Run:
 *   ./rv32_dpi_elf_test                    # self-test with generated ELF
 *   ./rv32_dpi_elf_test <path-to.elf>      # test with a real ELF
 */

#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DPI stubs */
static uint32_t mmio_region[64] = {0};

extern "C" int dpi_mmio_read(int addr) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        return (int)mmio_region[idx];
    }
    return 0;
}

extern "C" void dpi_mmio_write(int addr, int data) {
    if (addr >= 0x10000000 && addr < 0x10000100) {
        int idx = (addr - 0x10000000) / 4;
        mmio_region[idx] = (uint32_t)data;
    }
}

int main(int argc, char **argv) {
    const char *elf_path = argc > 1 ? argv[1] : NULL;

    if (elf_path) {
        /* Test with a provided ELF file */
        printf("=== Testing rv_init_elf with '%s' ===\n", elf_path);

        /* Use a large enough ram_size for programs linked at high addresses */
        uint32_t entry = rv_init_elf(elf_path, 1 << 20);
        if (entry == 0) {
            fprintf(stderr, "FAILED: rv_init_elf returned 0\n");
            return 1;
        }
        printf("Entry point: 0x%08x\n", entry);

        /* Print first 8 words of RAM at the entry point */
        uint32_t *ram = (uint32_t *)rv_get_ram();
        uint32_t *code_at_entry = (uint32_t *)((uint8_t *)ram + entry);
        printf("Code at entry (0x%08x): 0x%08x 0x%08x 0x%08x 0x%08x\n",
               entry, code_at_entry[0], code_at_entry[1],
               code_at_entry[2], code_at_entry[3]);

        /* Execute a few instructions */
        int executed = rv_step(100);
        printf("Executed %d instructions, PC=0x%08x\n", executed, rv_get_pc());

        if (executed == 0) {
            fprintf(stderr, "FAILED: no instructions executed\n");
            return 1;
        }

        printf("=== PASSED ===\n");
        return 0;
    }

    /* Self-test: generate a minimal ELF in memory, write to temp file, load it */
    printf("=== Self-test: generating minimal ELF ===\n");

    /* Create a minimal RV32 ELF with a simple program:
     *   lui a5, 0x10000     # a5 = 0x10000000 (MMIO base)
     *   addi a4, zero, 0x48 # a4 = 'H'
     *   sw a4, 0(a5)        # write to MMIO
     *   addi a4, zero, 0x21 # a4 = '!'
     *   sw a4, 0(a5)        # write to MMIO
     *   ebreak               # halt
     */
    uint32_t code[] = {
        0x100007b7,  // lui a5, 0x10000
        0x04800713,  // addi a4, zero, 0x48
        0x00e7a023,  // sw a4, 0(a5)
        0x02100713,  // addi a4, zero, 0x21
        0x00e7a023,  // sw a4, 0(a5)
        0x00100073,  // ebreak
    };
    size_t code_size = sizeof(code);

    /* Build a minimal ELF header + program header + code */
    /* ELF header: 52 bytes for 32-bit */
    /* Program header: 32 bytes */
    /* Total header size: 52 + 32 = 84 bytes */
    /* Code starts at offset 84, loaded at vaddr 0x00000000 */

    uint8_t elf_buf[4096];
    memset(elf_buf, 0, sizeof(elf_buf));

    /* ELF header (offsets 0-51) */
    elf_buf[0]  = 0x7f; elf_buf[1] = 'E'; elf_buf[2] = 'L'; elf_buf[3] = 'F';
    elf_buf[4]  = 1;    /* ELFCLASS32 */
    elf_buf[5]  = 1;    /* ELFDATA2LSB */
    elf_buf[6]  = 1;    /* EV_CURRENT */
    elf_buf[7]  = 0;    /* OS/ABI: System V */
    /* e_type (offset 16): ET_EXEC = 2 */
    elf_buf[16] = 2; elf_buf[17] = 0;
    /* e_machine (offset 18): EM_RISCV = 0xF3 */
    elf_buf[18] = 0xF3; elf_buf[19] = 0;
    /* e_version (offset 20): 1 */
    elf_buf[20] = 1; elf_buf[21] = 0; elf_buf[22] = 0; elf_buf[23] = 0;
    /* e_entry (offset 24): 0x00000000 */
    /* Already zeroed */
    /* e_phoff (offset 28): 52 (right after ELF header) */
    elf_buf[28] = 52; elf_buf[29] = 0; elf_buf[30] = 0; elf_buf[31] = 0;
    /* e_shoff (offset 32): 0 (no section headers) */
    /* Already zeroed */
    /* e_flags (offset 36): 0 */
    /* Already zeroed */
    /* e_ehsize (offset 40): 52 */
    elf_buf[40] = 52; elf_buf[41] = 0;
    /* e_phentsize (offset 42): 32 */
    elf_buf[42] = 32; elf_buf[43] = 0;
    /* e_phnum (offset 44): 1 */
    elf_buf[44] = 1; elf_buf[45] = 0;
    /* e_shentsize (offset 46): 0 */
    /* Already zeroed */
    /* e_shstrndx (offset 48): 0 */
    /* Already zeroed */
    /* e_shnum (offset 50): 0 */
    /* Already zeroed */

    /* Program header (offsets 52-83) */
    /* p_type (offset 52+0): PT_LOAD = 1 */
    elf_buf[52] = 1; elf_buf[53] = 0; elf_buf[54] = 0; elf_buf[55] = 0;
    /* p_offset (offset 52+4): 84 (right after headers) */
    elf_buf[56] = 84; elf_buf[57] = 0; elf_buf[58] = 0; elf_buf[59] = 0;
    /* p_vaddr (offset 52+8): 0 */
    /* Already zeroed */
    /* p_paddr (offset 52+12): 0 */
    /* Already zeroed */
    /* p_filesz (offset 52+16): code_size */
    elf_buf[68] = code_size & 0xFF;
    elf_buf[69] = (code_size >> 8) & 0xFF;
    elf_buf[70] = (code_size >> 16) & 0xFF;
    elf_buf[71] = (code_size >> 24) & 0xFF;
    /* p_memsz (offset 52+20): code_size */
    elf_buf[72] = code_size & 0xFF;
    elf_buf[73] = (code_size >> 8) & 0xFF;
    elf_buf[74] = (code_size >> 16) & 0xFF;
    elf_buf[75] = (code_size >> 24) & 0xFF;
    /* p_flags (offset 52+24): PF_R | PF_X = 5 */
    elf_buf[76] = 5; elf_buf[77] = 0; elf_buf[78] = 0; elf_buf[79] = 0;
    /* p_align (offset 52+28): 4 */
    elf_buf[80] = 4; elf_buf[81] = 0; elf_buf[82] = 0; elf_buf[83] = 0;

    /* Copy code at offset 84 */
    memcpy(&elf_buf[84], code, code_size);

    /* Write to temp file */
    const char *tmp_path = "/tmp/test_elf_loader.elf";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "FAILED: cannot create temp file\n");
        return 1;
    }
    fwrite(elf_buf, 1, 84 + code_size, f);
    fclose(f);

    printf("Wrote %zu bytes to %s\n", 84 + code_size, tmp_path);

    /* Now load it via rv_init_elf */
    uint32_t entry = rv_init_elf(tmp_path, 1 << 20);
    printf("Entry point: 0x%08x\n", entry);

    /* Verify code was loaded correctly */
    uint32_t *ram = (uint32_t *)rv_get_ram();
    printf("RAM[0..5]: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
           ram[0], ram[1], ram[2], ram[3], ram[4], ram[5]);

    /* Check first instruction matches */
    if (ram[0] != code[0]) {
        fprintf(stderr, "FAILED: RAM[0] = 0x%08x, expected 0x%08x\n", ram[0], code[0]);
        return 1;
    }
    printf("Code verification: PASSED\n");

    /* Execute */
    int executed = rv_step(100);
    printf("Executed %d instructions, PC=0x%08x\n", executed, rv_get_pc());

    if (executed == 0) {
        fprintf(stderr, "FAILED: no instructions executed\n");
        return 1;
    }

    printf("=== ALL TESTS PASSED ===\n");
    return 0;
}
