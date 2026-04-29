# DPI Verilator Harness

This harness demonstrates the RV32 DPI emulator in a minimal Verilator simulation.

## Build

From the repository root:

    cd dpi-riscv
    make

## Run

    ./obj_dir/Vtb_top

The harness writes a small program into shared RAM, executes it with `rv_step()`, and captures the MMIO write result in the SystemVerilog DPI-exported register.

---

## How firmware is loaded into the ISS

The SystemVerilog testbenches never handle firmware directly — they are purely passive MMIO bridges (providing `dpi_mmio_read()` / `dpi_mmio_write()` exports and importing `rv_set_irq()` for interrupts).

The executable code is loaded into the ISS (instruction-set simulator) **entirely from the C++ harness side**, using one of two strategies:

### Strategy 1: Load a precompiled binary file via `rv_init(path)`

Used by: `rv32_dpi_tb.cpp`, `rv32_dpi_irq_tb.cpp`, `rv32_dpi_ahb_tb.cpp`

The Makefile compiles RISC-V assembly source (`.S` files) into a flat binary using the RISC-V toolchain:

```
firmware.S → riscv64-unknown-elf-as   → firmware.o
           → riscv64-unknown-elf-ld   → firmware.elf
           → riscv64-unknown-elf-objcopy -O binary → firmware.bin
```

Then the C++ testbench calls:

```cpp
rv_init("firmware.bin", 1 << 20);   // reads the .bin file from disk into ISS RAM
rv_reset(0);                         // sets PC=0
rv_step(N);                          // starts executing
```

Inside `rv32_dpi.c`, `rv_init()` does `fopen("firmware.bin", "rb")` + `fread()` into its internal `memory[]` buffer. **No SV code is involved in loading firmware.**

### Strategy 2: Build instructions programmatically in C++ (hand-assembled)

Used by: `rv32_dpi_mmio_regs_tb.cpp`, `rv32_dpi_muldiv_tb.cpp` (and all standalone ISS tests like `rv32_dpi_test.cpp`, `rv32_dpi_c_test.cpp`, `rv32_dpi_irq_test.cpp`)

These call:

```cpp
rv_init(NULL, 1 << 20);                     // allocate RAM, don't load from file
uint32_t *ram = (uint32_t *)rv_get_ram();   // get pointer to ISS memory
build_firmware(ram);                        // write 32-bit instruction words directly
rv_reset(0);                                // set PC=0
rv_step(N);                                 // execute
```

The `build_firmware()` function uses helper macros like `make_addi()`, `make_lui()`, `make_sw()`, `make_ebreak()` to encode RISC-V machine code directly into the ISS RAM at C++ run time. No toolchain, no assembly, no binary files required.

### Why the SV testbenches don't see firmware

The SystemVerilog modules (`tb_top.sv`, `tb_top_mmio_regs.sv`, `tb_top_ahb.sv`) are **completely unaware** of the firmware. They only provide:

- **DPI exports** (`dpi_mmio_read()` / `dpi_mmio_write()`) — called by the ISS C code when the firmware executes loads/stores to the MMIO region.
- **DPI imports** (`rv_set_irq()`) — called by SV to inject interrupts into the ISS.

The firmware lives in the ISS's private `memory[]` buffer and is fetched/executed by `rv_step()` using only that buffer. The SV side never sees the instruction stream.

### Sequence diagram

```
  Makefile                  C++ harness               ISS (rv32_dpi.c)         SV (tb_top.sv)
   │                           │                           │                      │
   ├─ as → ld → objcopy ──────┤                           │                      │
   │     (produces .bin)       │                           │                      │
   │                           ├── rv_init("fw.bin") ────►│ fopen + fread         │
   │                           │                           │   → memory[]          │
   │                           ├── rv_reset(0) ──────────►│ pc = 0                │
   │                           │                           │                      │
   │                           ├── rv_step(N) ───────────►│ fetch memory[pc]      │
   │                           │                           │ decode & execute      │
   │                           │                           │   ├── MMIO load?      │
   │                           │                           │   │  → dpi_mmio_read()│──► SV export
   │                           │                           │   ├── MMIO store?     │
   │                           │                           │   │  → dpi_mmio_write()│──► SV export
   │                           │                           │   └── ebreak?        │
   │                           │◄──────────────────────────│      return           │
```

In summary: **the firmware is loaded into the ISS's internal memory by C++ calling `rv_init()` (from a binary file) or directly writing instructions via `rv_get_ram()` (programmatically), long before any SV simulation occurs. The SV layer only interacts with ISS memory when the running firmware performs MMIO accesses.**
