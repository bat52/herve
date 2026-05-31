# Running Zephyr RTOS on Herve

This document describes how to build and run [Zephyr RTOS](https://zephyrproject.org/)
applications on the Herve RISC-V ISS simulator.

## Overview

Herve supports Zephyr RTOS through:

- **RV32IM CPU** with machine-level CSRs (mstatus, mie, mtvec, mcause, mip, mcycle)
- **Machine timer** (`mtime`/`mtimecmp`) for OS tick generation
- **HTIF console** for serial I/O
- **Verilator simulation** with cycle-accurate timer peripheral

## Prerequisites

- [Zephyr RTOS 3.x+](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
- RISC-V toolchain (zephyr-sdk or `riscv64-unknown-elf-gcc`)
- Herve with Verilator (for timer-driven simulation)

## Board Support

Herve board files are in `zephyr/boards/riscv/herve/`. Use them with
Zephyr's out-of-tree board support:

```bash
# Using west
west build -b herve samples/hello_world \
  -- -DBOARD_ROOT=/path/to/herve/dpi-riscv/zephyr

# Or directly with cmake
cmake -B build -GNinja \
  -DBOARD=herve \
  -DBOARD_ROOT=/path/to/herve/dpi-riscv/zephyr \
  samples/hello_world
ninja -C build
```

## Running on Herve

### Standalone Mode (no timer interrupts)

For simple programs that don't need a timer (e.g., hello world):

```bash
cd dpi-riscv
make herve
./herve run build/zephyr/zephyr.elf --max-cycles=100000
```

### Verilator Simulation (with timer and interrupts)

For Zephyr applications that need the machine timer:

```bash
cd dpi-riscv
./obj_dir/Vtb_top_zephyr_boot build/zephyr/zephyr.elf -c 500000
```

Options:
- `-c <ticks>` — Maximum clock ticks (default: 500000)
- `--trace` — Enable VCD waveform dump

## Herve Zephyr Board Details

### Memory Map

| Address | Region | Description |
|---------|--------|-------------|
| `0x00000000` | RAM (16 MB) | Code, data, stack, heap |
| `0x10000000` | GPIO | GPIO_OUT/IE/STATUS |
| `0x10000010` | MTIME | 64-bit timer counter |
| `0x10000018` | MTIMECMP | 64-bit timer compare |
| `0x80001000` | HTIF | Console I/O |

### Device Tree

The Herve board device tree (`herve.dts`) defines:

- **CPU**: RV32IM, single core, machine mode
- **Timer**: `riscv,machine-timer` compatible at `0x10000018`
- **Console**: `riscv,htif` compatible at `0x80001000`
- **SRAM**: 16 MB at `0x00000000`

### Kconfig

Key configurations in `Kconfig.defconfig`:

- `CONFIG_RISCV_MACHINE_TIMER=y` — Enable machine timer driver
- `CONFIG_RISCV_HTIF=y` — Enable HTIF console
- `CONFIG_MINIMAL_LIBC=y` — Reduce image size

### Timer Frequency

The machine timer counter increments at the system clock rate
(one tick per clock cycle in Verilator simulation). Zephyr's
timer driver programs `mtimecmp = mtime + timeout` for each
OS tick. The default tick rate is configurable via
`CONFIG_SYS_CLOCK_TICKS_PER_SEC`.

## Testing

### Timer Self-Test

A firmware test (`firmware_timer.S`) verifies the timer IRQ path:

```bash
make run_timer
```

Expected output:
```
Timer IRQ fired at tick 50001! GPIO_OUT toggled to 1
mtime_lo at fire: 0x0000c351 (dec 50001)
PASS
```

### Boot Harness

The boot harness loads any RISC-V ELF and runs the timer-driven simulation:

```bash
make run_zephyr_boot
```

This runs `firmware_timer.elf` through the boot harness as a smoke test.

## Limitations

- Timer runs at instruction rate, not wall-clock time
- No UART — console is HTIF-only
- No PMP, virtual memory, or user mode
- Single-core only
- No device tree passed to Zephyr at runtime (static DTS)

## Profiling

Using the Herve profiler with the Zephyr blinky sample (architecture model `ibex_small`):

```bash
cd dpi-riscv
./herve run firmware_timer.elf --arch=ibex_small --profile --out=profile.csv
```

### Top CPU consumers

| Rank | Function | Total cycles |
|------|----------|-------------:|
| 1 | `strcmp` | 102,000 |
| 2 | `main` | 77,088 |
| 3 | `Proc_1` | 52,000 |
| 4 | `Proc_8` | 19,500 |
| 5 | `Func_2` | 17,500 |

### Top ISA instruction types by cycles

| Instruction | Cycles |
|-------------|--------:|
| LOAD | highest |
| BRANCH | 40,000 (`strcmp`) |
| ALU | 20,500 (`strcmp`) |

### Profile summary

- Architecture: `ibex_small` (RV32IM)
- Total cycles: 347,310
- Total instructions: 198,706

In this blinky workload, `strcmp` dominates execution time, and memory accesses (`LOAD`) are the most cycle-intensive instruction class.
