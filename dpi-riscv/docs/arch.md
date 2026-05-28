# Architecture Overview

## Memory Map

| Range | Description | Mode |
|-------|-------------|------|
| `0x0000_0000` - `0x000F_FFFF` | Shared RAM (1 MB default) | R/W/X |
| `0x1000_0000` - `0x100F_FFFF` | MMIO Peripheral Region (1 MB) | R/W |
| `0x1000_0000` | GPIO_OUT — Output value (bit 0 = LED/ext_irq) | R/W |
| `0x1000_0004` | GPIO_IE — Interrupt enable | R/W |
| `0x1000_0008` | GPIO_STATUS — Interrupt status | R |
| `0x1000_0010` | MTIME — 64-bit machine timer counter (LO) | R |
| `0x1000_0014` | MTIME — 64-bit machine timer counter (HI) | R |
| `0x1000_0018` | MTIMECMP — 64-bit timer compare (LO) | R/W |
| `0x1000_001C` | MTIMECMP — 64-bit timer compare (HI) | R/W |
| `0x8000_1000` | HTIF — Host-Target Interface (tohost/fromhost) | R/W |

## Machine Timer

The machine timer implements the standard RISC-V `mtime`/`mtimecmp`
registers. The 64-bit counter increments on every system clock cycle
in the Verilator simulation. When `mtime ≥ mtimecmp`, the machine timer
interrupt (cause 7) is asserted.

The timer interrupt is delivered to the ISS via `rv_set_irq(0x80)`
on every positive clock edge. The ISS checks for pending interrupts
at the start of each `rv_step()` batch, respecting the `mie.MTIE`
(bit 7) enable and `mstatus.MIE` (bit 3) global enable.

## CSR Support

| CSR | Address | Description |
|-----|---------|-------------|
| `mstatus` | 0x300 | Machine status (MIE bit 3) |
| `mie` | 0x304 | Machine interrupt-enable (MTIE bit 7) |
| `mtvec` | 0x305 | Machine trap-vector base (direct/vectored mode) |
| `mepc` | 0x341 | Machine exception PC |
| `mcause` | 0x342 | Machine exception cause |
| `mip` | 0x344 | Machine interrupt pending (MTIP bit 7) |
| `mcycle` | 0xB00 | Cycle counter |
| `minstret` | 0xB02 | Instruction count |

## Interrupt Handling

Interrupts are delivered to the ISS via `rv_set_irq(mask)` where each
bit corresponds to an interrupt cause. Currently supported sources:

| Bit | Cause | Source |
|-----|-------|--------|
| 0 | External | GPIO pin |
| 7 | Machine Timer | mtime ≥ mtimecmp |

The ISS checks for pending interrupts at the start of each `rv_step()`
batch. Only interrupts enabled in both `mie` and `mstatus.MIE` are
considered. The `mtvec` register controls interrupt vectoring:
- **Direct mode** (`mtvec[0] = 0`): All interrupts jump to `mtvec`
- **Vectored mode** (`mtvec[0] = 1`): Cause `i` jumps to `mtvec + 4i`

## Synchronization

The ISS runs in batches of instructions. By default, it runs 1000 instructions before yielding to the SystemVerilog/Verilator simulation.

- **MMIO Access**: Any memory access inside `0x1000_0000` – `0x100F_FFFF` is treated as MMIO. This triggers a DPI-C call to the RTL, which may block until the RTL transaction completes.
- **Interrupts**: IRQs from the RTL are checked at the start of each `rv_step()` batch, or when the ISS is waiting for MMIO.
