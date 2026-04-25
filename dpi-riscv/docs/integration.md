# Integration Guide

## RTL Side (SystemVerilog)

The RTL top level must export MMIO read/write functions and import ISS control functions.

```systemverilog
// Exported to C (ISS)
export "DPI-C" function int dpi_mmio_read(int addr);
export "DPI-C" function void dpi_mmio_write(int addr, int data);

// Imported from C (ISS)
import "DPI-C" function void rv_init(string fw, int ram_sz);
import "DPI-C" function int  rv_step(int max_insn);
```

## Software Side (C)

Firmware should use the defined memory map for peripherals.

```c
#define GPIO_BASE 0x40000000
#define REG_WRITE(addr, val) (*((volatile uint32_t *)(addr)) = (val))

void main() {
    REG_WRITE(GPIO_BASE, 0x1); // This triggers a DPI-C call to RTL
}
```
