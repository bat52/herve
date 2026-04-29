# Architecture Overview

## Memory Map

| Range | Description | Mode |
|-------|-------------|------|
| `0x0000_0000` - `0x000F_FFFF` | Shared RAM (1 MB default) | R/W/X |
| `0x1000_0000` - `0x100F_FFFF` | MMIO Peripheral Region (1 MB) | R/W |
| `0x1000_0000` | GPIO_OUT — Output value (bit 0 = LED/ext_irq) | R/W |
| `0x1000_0004` | GPIO_IE — Interrupt enable | R/W |
| `0x1000_0008` | GPIO_STATUS — Interrupt status | R |

## Synchronization

The ISS runs in batches of instructions. By default, it runs 1000 instructions before yielding to the SystemVerilog/Verilator simulation.

- **MMIO Access**: Any memory access inside `0x1000_0000` – `0x100F_FFFF` is treated as MMIO. This triggers a DPI-C call to the RTL, which may block until the RTL transaction completes.
- **Interrupts**: IRQs from the RTL are checked at the start of each `rv_step()` batch, or when the ISS is waiting for MMIO.
