# Implementation Plan: Port DPI Tests to ModelSim (Free Edition)

## Overview

Port all existing Verilator DPI-C tests to ModelSim Intel FPGA Starter Edition
using pure SystemVerilog DPI-C testbenches, a C support library, and a unified
shell runner script.

The existing test suite covers five scenarios: basic MMIO smoke test, MMIO
register configuration, MUL/DIV extension verification, IRQ injection with WFI,
and AHB-Lite GPIO self-test. Each is currently driven by a C++ harness that
controls both the RTL model (via Verilator) and the ISS (via direct C calls).
Porting to ModelSim requires replacing the C++ harness with SystemVerilog
`initial` blocks that drive the simulation through imported DPI-C functions,
while keeping the same ISS C code (`rv32_dpi.c`) and firmware binaries compiled
with the RISC-V toolchain.

The AHB test requires special treatment: the existing C++ harness drives the
AHB Bus Functional Model through a request/ack handshake from inside the
`dpi_mmio_read`/`dpi_mmio_write` exports (ticking the clock inline). This
multi-cycle timing pattern is not possible in pure DPI-C functions (which must
be non-blocking). The port solves this by using a register-shadow approach that
mimics the GPIO peripheral behavior, testing the same end-to-end IRQ flow
without requiring the AHB BFM to be driven from a DPI context.

## Types

Three new DPI-C callable C functions exposed to SystemVerilog via a support
library.

| C Function | Signature | Purpose |
|---|---|---|
| `rv_mti_write_ram` | `void(int word_offset, int value)` | Write a 32-bit word to ISS RAM at `word_offset * 4` |
| `rv_mti_set_irq` | `void(int mask)` | Wrapper around `rv_set_irq()` for direct SV import |
| (existing) | `rv_init`, `rv_reset`, `rv_step`, `rv_get_pc`, `rv_get_reg` | Already declared in `rv32_dpi.h`, compiled as DPI-C imports |

## Files

### New Files

| File | Purpose |
|---|---|
| `dpi-riscv/firmware_mmio_regs.S` | RISC-V assembly for the MMIO register config test (handles writes to four MMIO registers, readback, ebreak) |
| `dpi-riscv/sim/modelsim/rv32_dpi_mti.c` | C support library for ModelSim DPI-C: provides `rv_mti_write_ram()` and `rv_mti_set_irq()` as DPI-C importable functions |
| `dpi-riscv/sim/modelsim/tb_modelsim_basic.sv` | Self-contained SV testbench: basic MMIO smoke test using `firmware.bin` |
| `dpi-riscv/sim/modelsim/tb_modelsim_mmio_regs.sv` | Self-contained SV testbench: MMIO register config using `firmware_mmio_regs.bin` |
| `dpi-riscv/sim/modelsim/tb_modelsim_muldiv.sv` | Self-contained SV testbench: MUL/DIV extension using `firmware_muldiv.bin` |
| `dpi-riscv/sim/modelsim/tb_modelsim_irq.sv` | Self-contained SV testbench: IRQ + WFI using `firmware_irq.bin` |
| `dpi-riscv/sim/modelsim/tb_modelsim_ahb.sv` | Self-contained SV testbench: AHB GPIO IRQ self-test using `firmware_ahb.bin` |
| `dpi-riscv/tests/run_modelsim.sh` | Shell runner: detects ModelSim toolchain, builds firmware, runs all tests, prints summary |
| `dpi-riscv/install_modelsim.sh` | Install script: downloads and installs ModelSim Intel FPGA Starter Edition (free) |

### Modified Files

| File | Change |
|---|---|
| `dpi-riscv/Makefile` | Add `firmware_mmio_regs` target (`.S` → `.bin`) |
| `dpi-riscv/Makefile` | Add `clean` entry for `firmware_mmio_regs.bin`, `firmware_mmio_regs.elf`, `firmware_mmio_regs.o` |

## Functions

### C Support Functions (in `rv32_dpi_mti.c`)

| Function | Signature | Details |
|---|---|---|
| `rv_mti_write_ram` | `void rv_mti_write_ram(int word_offset, int value)` | Gets pointer from `rv_get_ram()`, writes `value` at `((uint32_t*)ram)[word_offset]`. Intended for pre-loading instruction sequences into ISS RAM. |
| `rv_mti_set_irq` | `void rv_mti_set_irq(int mask)` | Thin wrapper calling `rv_set_irq((uint32_t)mask)`. Necessary because `rv_set_irq` takes `uint32_t` and the direct DPI-C import might have type-matching issues with SV `int`. |

### SV Testbench Structure (common pattern in all 5 files)

Each SV testbench module follows this pattern:

```
1. Module header with no ports (self-contained)
2. DPI-C imports:
   - `import "DPI-C" function void rv_init(string firmware, int ram_size);`
   - `import "DPI-C" function void rv_reset(int pc);`
   - `import "DPI-C" function int rv_step(int max_insn);`
   - `import "DPI-C" function int rv_get_pc();`
   - `import "DPI-C" function void rv_mti_write_ram(int word_offset, int value);`
   - `import "DPI-C" function void rv_mti_set_irq(int mask);`
3. DPI-C exports:
   - `export "DPI-C" function dpi_mmio_read;`
   - `export "DPI-C" function dpi_mmio_write;`
4. Local MMIO register file: `reg [31:0] mmio_regs [0:63];`
5. DPI export implementations (with appropriate address decoding per test)
6. Clock generation: always #5 clk = ~clk; (100 MHz)
7. Test sequence in `initial` block:
   a. Load firmware via `rv_init()` or write instructions via `rv_mti_write_ram()`
   b. Reset ISS via `rv_reset(0)`
   c. Execute via `rv_step(N)`
   d. (For IRQ test) Drive IRQ signal and step again
   e. Verify MMIO register values against expected results
   f. Print [PASS]/[FAIL] and `$finish`
```

### Per-test details

**tb_modelsim_basic.sv:**
- Loads `firmware.bin` via `rv_init("firmware.bin", 1<<20)`
- MMIO at 0x1000_0000: dpi_mmio_read returns 0 (no input drive needed for smoke test)
- Steps 64 instructions
- Verifies `mmio_regs[0] == 0x0A` (newline), `mmio_regs[1] == 0xDEAD_BEEF`, `mmio_regs[2] == 0x12345678`

**tb_modelsim_mmio_regs.sv:**
- Loads `firmware_mmio_regs.bin` via `rv_init()`
- MMIO at 0x1000_0000-0x1000_000F: array of 4 registers
- Steps 32 instructions
- Verifies `mmio_regs[0..3] == {0xAAAA_AAAB, 0x5A5, 0x5555_5554, 0x7FF}`

**tb_modelsim_muldiv.sv:**
- Loads `firmware_muldiv.bin` via `rv_init()`
- MMIO at 0x1000_0000-0x1000_00FF: array of 64 registers
- Steps 200 instructions
- Verifies 16 expected MUL/DIV/REM results in regs[0..15]

**tb_modelsim_irq.sv:**
- Loads `firmware_irq.bin` via `rv_init()`
- MMIO at 0x1000_0000 (GPIO_OUT R/W), 0x1000_0004 (GPIO_IE R/W), 0x1000_0008 (GPIO_STATUS R)
- Phase 1: Step ~100 instructions (firmware boots, configures mtvec/MIE/GPIO_IE, enters WFI)
- Assert IRQ: set a local `irq_pending` flag, call `rv_mti_set_irq(1)`
- Phase 2: Step ~100 instructions (ISS wakes, vectors to handler, toggles GPIO_OUT, mret)
- Verify GPIO_OUT toggled: `mmio_regs[0] == 1`
- Verify GPIO_STATUS reflects irq: updates automatically

**tb_modelsim_ahb.sv:**
- Loads `firmware_ahb.bin` via `rv_init()`
- MMIO register shadow with GPIO behavior:
  - `mmio_regs[0]` = GPIO_OUT (at 0x1000_0000)
  - `mmio_regs[1]` = GPIO_IE  (at 0x1000_0004)
  - `mmio_regs[2]` = GPIO_STATUS (at 0x1000_0008, bit 0 = `ext_irq`)
  - `mmio_regs[0x40]` = pass indicator (at 0x1000_0100, word index 64)
  - When GPIO_OUT[0] is written 1, auto-assert `ext_irq` flag and call `rv_mti_set_irq(1)`
  - When GPIO_OUT[0] is written 0, de-assert `ext_irq` flag and call `rv_mti_set_irq(0)`
- Phase 1: Step ~100 instructions (boot, configure, write GPIO_OUT=1)
- Phase 2: Step ~100 instructions (IRQ fires, handler reads STATUS, clears GPIO_OUT)
- Phase 3: Step ~100 instructions (post-handler, write pass indicator)
- Verify: `mmio_regs[0] == 0` (GPIO_OUT cleared), `mmio_regs[0x40] == 1` (pass)

## Classes

No new classes. This is a procedural C + SV project.

## Dependencies

No new packages. The test runner script `run_modelsim.sh` will check for:
- `vlib`, `vlog`, `vsim` commands (ModelSim Intel FPGA Starter Edition)
- `gcc` (for compiling the C support library and `rv32_dpi.c`)
- RISC-V toolchain (`riscv64-unknown-elf-as`, `riscv64-unknown-elf-ld`, `riscv64-unknown-elf-objcopy`) for firmware build

## Testing

Each SV testbench prints [PASS]/[FAIL] for individual checks and exits with
code 0 on full pass, non-zero on failure. The `run_modelsim.sh` script runs
all five tests and reports a summary.

ModelSim is invoked in command-line/capture mode (`vsim -c -do "run -all; quit"`).
The DPI-C shared library is compiled with `gcc -shared -fPIC -I./sim/iss` and
loaded via `vsim -sv_lib`.

## Implementation Order

1. Create `install_modelsim.sh` — download and install ModelSim Intel FPGA Starter Edition (free)
2. Create `rv32_dpi_mti.c` — the C support library for ModelSim DPI-C
3. Create `firmware_mmio_regs.S` — additional firmware assembly for the MMIO regs test
4. Add `firmware_mmio_regs` target to `Makefile` and `clean` entry
5. Create `tb_modelsim_basic.sv` — simplest test case to verify the DPI-C toolchain
6. Create `tb_modelsim_mmio_regs.sv` — MMIO register config test
7. Create `tb_modelsim_muldiv.sv` — MUL/DIV extension test
8. Create `tb_modelsim_irq.sv` — IRQ+WFI test
9. Create `tb_modelsim_ahb.sv` — AHB GPIO self-test (most complex, register-shadow approach)
10. Create `tests/run_modelsim.sh` — test runner with tool detection and summary
11. Manual: run `tests/run_modelsim.sh` to validate all tests pass
