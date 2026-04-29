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
#define MMIO_BASE   0x10000000
#define GPIO_OUT    (MMIO_BASE + 0x000)
#define GPIO_IE     (MMIO_BASE + 0x004)
#define GPIO_STATUS (MMIO_BASE + 0x008)

#define REG_WRITE(addr, val) (*((volatile uint32_t *)(addr)) = (val))

void main() {
    REG_WRITE(GPIO_OUT, 0x1); // Toggle GPIO — triggers a DPI-C call to RTL
}
```

## MMIO Address Map

The ISS recognizes MMIO accesses in the range `0x1000_0000` – `0x100F_FFFF` (defined by `MMIO_BASE = 0x10000000` and `MMIO_SIZE = 0x00100000` in `rv32_dpi.c`). Any load or store to this region triggers a DPI-C call to the RTL via `dpi_mmio_read()` / `dpi_mmio_write()`.

| Address | Register | Width | Access | Description |
|---------|----------|-------|--------|-------------|
| `0x1000_0000` | `GPIO_OUT` | 32 | R/W | Output value (bit 0 = LED / ext_irq) |
| `0x1000_0004` | `GPIO_IE` | 32 | R/W | Interrupt enable |
| `0x1000_0008` | `GPIO_STATUS` | 32 | R | Interrupt status (bit 0 = IRQ pending) |

> **Note:** The old example addresses `0x4000_0000` (GPIO) and `0x4000_1000` (Timer) have been replaced by the current MMIO region at `0x1000_0000`.
