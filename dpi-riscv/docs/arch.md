# Architecture Overview

## Memory Map

| Range | Description | Mode |
|-------|-------------|------|
| `0x0000_0000` - `0x0000_FFFF` | Shared RAM (64KB) | R/W/X |
| `0x4000_0000` - `0x4000_000F` | GPIO Peripheral (Example) | R/W |
| `0x4000_1000` - `0x4000_100F` | Timer Peripheral (Example) | R/W |

## Synchronization

The ISS runs in batches of instructions. By default, it runs 1000 instructions before yielding to the SystemVerilog/Verilator simulation.

- **MMIO Access**: Any memory access outside the Shared RAM range is treated as MMIO. This triggers a DPI-C call to the RTL, which may block until the RTL transaction completes.
- **Interrupts**: IRQs from the RTL are checked at the end of each `rv_step` or when the ISS is waiting for MMIO.
