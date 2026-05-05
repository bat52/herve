# ModelSim DPI-C Integration

## Overview

Herve can be embedded into **ModelSim** (Intel FPGA Edition) simulations via a DPI-C shared library. This allows running RISC-V firmware directly inside ModelSim testbenches, with MMIO reads/writes bridged between the ISS and SystemVerilog via DPI-C export/import.

The integration provides:

- **5 pre-built testbenches** covering basic MMIO, MUL/DIV, IRQ, and AHB-Lite GPIO scenarios
- **Batch test runner** (`tests/run_modelsim.sh`) for automated regression testing
- **32-bit DPI-C shared library** (`sim/modelsim/rv32_dpi_mti.so`) compiled with `-m32`

## Prerequisites

### ModelSim ASE (Altera/Intel FPGA Starter Edition)

ModelSim Intel FPGA Starter Edition is a **free** simulation tool available from Intel. It is a **32-bit application**, which requires 32-bit compatibility libraries on 64-bit Linux systems.

### 32-bit Compatibility Libraries

On a 64-bit Debian/Ubuntu system, install the required 32-bit libraries:

```bash
sudo bash tests/install_modelsim_deps.sh
```

This script:
1. Enables the `i386` architecture
2. Updates package lists
3. Installs `libc6:i386`, `lib32stdc++6`, `libxext6:i386`, `libxft2:i386`, `libx11-6:i386`, `libxrender1:i386`, `libfontconfig1:i386`, `libc6-dev:i386`, and `gcc-multilib`

To uninstall these dependencies:

```bash
sudo bash tests/uninstall_modelsim_deps.sh
```

### RISC-V Toolchain

Required for building firmware binaries:

```bash
sudo apt install gcc-riscv64-unknown-elf binutils-riscv64-linux-gnu
```

### gcc (with multilib support)

Required for compiling the 32-bit DPI-C shared library. Installed as part of `install_modelsim_deps.sh` above, or manually:

```bash
sudo apt install gcc-multilib
```

## Quick Start

### 1. Build firmware binaries

```bash
cd dpi-riscv
make firmware firmware_muldiv firmware_irq firmware_ahb firmware_mmio_regs
```

### 2. Compile the DPI-C shared library

```bash
gcc -shared -fPIC -m32 -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
    sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
```

### 3. Run a single testbench (e.g., basic MMIO smoke test)

```bash
cd sim/modelsim
vlib work
vlog tb_modelsim_basic.sv
vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_basic
```

Expected output:

```
RESULT: PASS
```

### 4. Run all tests

```bash
bash tests/run_modelsim.sh
```

## DPI-C API Reference

### Imported Functions (C → SV)

These functions are implemented in `rv32_dpi_mti.c` and imported by SystemVerilog testbenches:

| Function | Signature | Description |
|----------|-----------|-------------|
| `rv_init` | `void rv_init(string firmware, int ram_size)` | Load firmware binary into ISS RAM |
| `rv_reset` | `void rv_reset(int pc)` | Reset ISS to given Program Counter |
| `rv_step` | `int rv_step(int max_insn)` | Execute up to `max_insn` instructions, returns count |
| `rv_get_pc` | `int rv_get_pc()` | Read current Program Counter |
| `rv_mti_write_ram` | `void rv_mti_write_ram(int word_offset, int value)` | Write a 32-bit word to ISS RAM at `word_offset * 4` |
| `rv_mti_set_irq` | `void rv_mti_set_irq(int mask)` | Set interrupt bitmask (bit 0 = external IRQ) |

### Exported Functions (SV → C)

These functions are defined in SystemVerilog and callable from the ISS C code via DPI-C export:

| Function | Signature | Description |
|----------|-----------|-------------|
| `dpi_mmio_read` | `int dpi_mmio_read(int addr)` | Read MMIO register at `addr` |
| `dpi_mmio_write` | `void dpi_mmio_write(int addr, int data)` | Write `data` to MMIO register at `addr` |

The ISS triggers these exports on any load/store to the MMIO address range `0x1000_0000` – `0x100F_FFFF`.

## Testbenches

All testbenches are located in `sim/modelsim/` and are pure SystemVerilog (no C++ harness needed).

| Testbench | Firmware | Description |
|-----------|----------|-------------|
| `tb_modelsim_basic.sv` | `firmware.bin` | Basic MMIO smoke test — writes known values to MMIO registers and verifies them |
| `tb_modelsim_mmio_regs.sv` | `firmware_mmio_regs.bin` | MMIO register config test — writes 4 specific patterns and verifies |
| `tb_modelsim_muldiv.sv` | `firmware_muldiv.bin` | MUL/DIV extension test — verifies 16 MUL/DIV/REM results including edge cases (division by zero, INT32_MIN / -1) |
| `tb_modelsim_irq.sv` | `firmware_irq.bin` | IRQ + WFI test — boots firmware, injects external interrupt via `rv_mti_set_irq()`, verifies handler toggles GPIO_OUT |
| `tb_modelsim_ahb.sv` | `firmware_ahb.bin` | AHB GPIO self-test — uses register-shadow logic to auto-assert/de-assert IRQ based on GPIO_OUT writes |

## Batch Test Runner

The `tests/run_modelsim.sh` script automates the full test workflow:

1. **Tool detection** — Finds ModelSim (`vsim`, `vlib`, `vlog`) via:
   - `MODELSIM_DIR` environment variable (explicit path)
   - `INTELFPGA_DIR` environment variable (auto-discovers latest version)
   - Common hardcoded paths (fallback)
   - `PATH` lookup (final fallback)
2. **Dependency checks** — Verifies `gcc` and RISC-V toolchain are available
3. **Firmware build** — Builds all firmware binaries via `make`
4. **Shared library build** — Compiles `rv32_dpi_mti.so` with `-m32`
5. **Test execution** — Runs each testbench, checks for `RESULT: PASS` in the simulation log
6. **Summary** — Prints PASS/FAIL/SKIP counts

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `INTELFPGA_DIR` | `$HOME/intelFPGA` | Path to Intel FPGA installation root |
| `MODELSIM_DIR` | — | Path to ModelSim installation (takes precedence over `INTELFPGA_DIR`) |
| `RISCV_PREFIX` | `riscv64-unknown-elf` | RISC-V toolchain prefix |

### Usage

```bash
# Run all tests (auto-detect ModelSim)
bash tests/run_modelsim.sh

# Specify ModelSim location explicitly
MODELSIM_DIR=/opt/intelFPGA/20.1/modelsim_ase/linuxaloem bash tests/run_modelsim.sh

# Specify Intel FPGA directory
INTELFPGA_DIR=$HOME/intelFPGA bash tests/run_modelsim.sh
```

## Project Structure

ModelSim-specific files in the repository:

```
dpi-riscv/
├── sim/
│   └── modelsim/
│       ├── rv32_dpi_mti.c              # DPI-C bridge library
│       ├── rv32_dpi_mti.so             # Compiled shared library (32-bit)
│       ├── tb_modelsim_basic.sv        # Basic MMIO smoke test
│       ├── tb_modelsim_mmio_regs.sv    # MMIO register config test
│       ├── tb_modelsim_muldiv.sv       # MUL/DIV extension test
│       ├── tb_modelsim_irq.sv          # IRQ + WFI test
│       ├── tb_modelsim_ahb.sv          # AHB GPIO self-test
│       └── implementation_plan_modelsim.md  # Implementation notes
├── tests/
│   ├── install_modelsim_deps.sh        # 32-bit dependency installer
│   ├── uninstall_modelsim_deps.sh      # 32-bit dependency uninstaller
│   └── run_modelsim.sh                 # Batch test runner
└── docs/
    └── modelsim.md                     # This file
```

## Troubleshooting

### "vlib: error while loading shared libraries"

ModelSim ASE is a 32-bit application. On a 64-bit system, you need 32-bit compatibility libraries:

```bash
sudo bash tests/install_modelsim_deps.sh
```

### "vlib: command not found" or "vsim: command not found"

ModelSim tools are not in `PATH`. Set `INTELFPGA_DIR` or `MODELSIM_DIR`:

```bash
INTELFPGA_DIR=$HOME/intelFPGA bash tests/run_modelsim.sh
```

### "gcc: error: unrecognized command-line option '-m32'"

The `gcc-multilib` package is missing. Install it:

```bash
sudo apt install gcc-multilib
```

### "rv_step returned 0 instructions"

The firmware binary may not have been loaded correctly. Verify:
- The firmware `.bin` file exists and is in the working directory
- The `rv_init()` call uses the correct filename
- The RAM size is sufficient (at least `1 << 20` = 1 MiB)

### Test shows "[SKIP] no ModelSim" in all tests

The `run_modelsim.sh` script detected ModelSim tools but they failed to execute (likely missing 32-bit libraries). Run the dependency installer:

```bash
sudo bash tests/install_modelsim_deps.sh
```

### "Error: (vsim-7) Failed to open shared library"

The DPI-C shared library (`rv32_dpi_mti.so`) was not found or could not be loaded. Ensure:
- The `.so` file exists in the working directory
- It was compiled with `-m32` (32-bit, matching ModelSim's architecture)
- It was compiled with `-fPIC`
