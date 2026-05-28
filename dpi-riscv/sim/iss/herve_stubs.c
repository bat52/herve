#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t dpi_mmio_read(uint32_t addr) {
    // printf("MMIO read at 0x%08x\n", addr);
    return 0;
}

void dpi_mmio_write(uint32_t addr, uint32_t value) {
    // printf("MMIO write at 0x%08x: 0x%08x\n", addr, value);
    // UART-like output at 0x10000000 is handled in the ISS, but keep stub for any remaining MMIO writes
    (void)addr;
    (void)value;
}
