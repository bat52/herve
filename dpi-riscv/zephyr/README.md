# Zephyr RTOS Board Support for Herve RISC-V ISS

This directory contains board support files to build [Zephyr RTOS](https://zephyrproject.org/)
applications for the Herve RISC-V ISS simulator.

## Directory Structure

```
zephyr/
├── boards/
│   └── riscv/
│       └── herve/
│           ├── herve.dts       # Device tree
│           ├── herve.yaml       # Board metadata
│           ├── Kconfig.board    # Board Kconfig
│           ├── Kconfig.defconfig # Default configuration
│           ├── board.cmake      # CMake build support
│           └── board.c          # Board init
└── README.md
```

## Prerequisites

- [Zephyr RTOS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) 3.x or later
- RISC-V toolchain (e.g., `zephyr-sdk` or `riscv64-unknown-elf-gcc`)
- Herve ISS simulator (this project)

## Building a Zephyr Application for Herve

### Method 1: Copy into Zephyr tree

```bash
# Clone Zephyr if you haven't already
cd ~
west init zephyrproject
cd zephyrproject
west update

# Copy the Herve board support
cp -r /path/to/herve/dpi-riscv/zephyr/boards/riscv/herve \
      zephyr/boards/riscv/herve

# Build a sample application
cd zephyr
west build -b herve samples/hello_world
```

### Method 2: Out-of-tree board (using BOARD_ROOT)

```bash
# Build with out-of-tree board support
west build -b herve samples/hello_world \
  -- -DBOARD_ROOT=/path/to/herve/dpi-riscv/zephyr
```

Or with CMake directly:

```bash
cmake -B build -GNinja \
  -DBOARD=herve \
  -DBOARD_ROOT=/path/to/herve/dpi-riscv/zephyr \
  samples/hello_world
ninja -C build
```

## Running on Herve

The build produces `zephyr/zephyr.elf` which can be loaded directly into
the Herve ISS using `rv_init_elf()`:

```bash
cd /path/to/herve/dpi-riscv

# Build Herve CLI
make herve

# Run Zephyr binary (standalone, no Verilator)
./herve run path/to/zephyr.elf --max-cycles=100000
```

Or with the Verilator simulation (with full timer and interrupt support):

```bash
# Use the zephyr test harness (to be implemented)
make run_zephyr
```

## Herve Memory Map

| Address       | Region       | Description        |
|---------------|--------------|--------------------|
| `0x00000000`  | RAM (16 MB)  | Code and data      |
| `0x10000000`  | MMIO region  | GPIO peripherals   |
| `0x10000010`  | MTIME        | 64-bit timer counter |
| `0x10000018`  | MTIMECMP     | 64-bit timer compare |
| `0x80001000`  | HTIF         | Console I/O        |

## Supported Features

| Feature           | Status |
|-------------------|--------|
| RV32IM CPU        | ✅     |
| Machine Timer     | ✅     |
| HTIF Console      | ✅     |
| GPIO              | ✅     |
| Interrupts (direct mode) | ✅ |
| Multi-threading   | ✅     |
| SMP               | ❌     |

## Known Limitations

- The timer counter increments at instruction rate (1 cycle/instruction),
  not wall-clock time. This is sufficient for functional testing.
- No UART peripheral — console is via HTIF.
- No PMP or virtual memory — machine mode only.
- Single-core only.
