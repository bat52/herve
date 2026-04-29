# Integration Guide

## RTL Side (SystemVerilog) — Verilator

When using **Verilator**, the RTL top level must export MMIO read/write functions and import ISS control functions via DPI-C.

```systemverilog
// Exported to C (ISS)
export "DPI-C" function int dpi_mmio_read(int addr);
export "DPI-C" function void dpi_mmio_write(int addr, int data);

// Imported from C (ISS)
import "DPI-C" function void rv_init(string fw, int ram_sz);
import "DPI-C" function int  rv_step(int max_insn);
```

## RTL Side (SystemVerilog) — Icarus Verilog

When using **Icarus Verilog**, DPI-C import/export declarations are **not supported** (confirmed on Icarus 12.0 stable from Ubuntu repositories). Instead, use **VPI (Verilog Procedural Interface)** system functions via a shared library module.

See `sim/vpi/rv32_dpi_vpi.c` for the VPI wrapper implementation.

Available VPI system functions/tasks:

| VPI Call | Type | Description |
|----------|------|-------------|
| `$rv_init(fw, ram_sz)` | task | Load firmware binary into ISS |
| `$rv_reset(pc)` | task | Reset ISS to given Program Counter |
| `$rv_step(max_insn)` | function → int | Execute up to max_insn instructions |
| `$rv_get_reg(reg)` | function → int | Read x[reg] value |
| `$rv_get_pc()` | function → int | Read current Program Counter |
| `$vpi_read_mmio(idx)` | function → int | Read MMIO register at index idx |
| `$vpi_print_mmio()` | task | Print MMIO register state to simulator log |

The VPI shared library is registered via the `vlog_startup_routines[]` array in `rv32_dpi_vpi.c`, which Icarus vvp calls at startup. The module is loaded at runtime with:

```bash
vvp -Mobj_dir -mrv32_dpi_vpi obj_dir/tb_icarus.vvp
```

### Icarus Limitations

| Limitation | Impact | Workaround |
|------------|--------|------------|
| No DPI-C import/export | Cannot use `import "DPI-C"` or `export "DPI-C"` declarations | Use VPI system functions instead (see above) |
| No `string` type support for VPI return | Cannot return strings from VPI functions | Use `vpiIntVal` for integer returns only |
| MMIO register file is C-side only | No SV-side access to MMIO registers in Icarus | Use `$vpi_read_mmio(idx)` and `$vpi_print_mmio()` for verification |
| Single VPI shared library | All ISS state is in the shared library process | Each `vvp` invocation is independent — no state sharing |
| No `--timing` support for VPI | Same as Icarus base — no SystemVerilog timing constructs | Use blocking assignments in `initial` blocks |

### Running the Icarus VPI Smoke Test

```bash
cd dpi-riscv
make firmware
make run_icarus
```

Expected output:
```
  Icarus Verilog VPI Smoke Test

ISS initialized, RAM = 1 MiB
Executing firmware...
rv_step: executed 20 instructions
PC after execution: 0x00000050

--- Verification ---
[PASS] mmio_regs[0] (newline) = 0x0000000a
[PASS] mmio_regs[1] = 0xdeadbeef
[PASS] mmio_regs[2] = 0x12345678

  RESULT: PASS
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

The ISS recognizes MMIO accesses in the range `0x1000_0000` – `0x100F_FFFF` (defined by `MMIO_BASE = 0x10000000` and `MMIO_SIZE = 0x00100000` in `rv32_dpi.c`). Any load or store to this region triggers a DPI-C/VPI call to the RTL via `dpi_mmio_read()` / `dpi_mmio_write()`.

| Address | Register | Width | Access | Description |
|---------|----------|-------|--------|-------------|
| `0x1000_0000` | `GPIO_OUT` | 32 | R/W | Output value (bit 0 = LED / ext_irq) |
| `0x1000_0004` | `GPIO_IE` | 32 | R/W | Interrupt enable |
| `0x1000_0008` | `GPIO_STATUS` | 32 | R | Interrupt status (bit 0 = IRQ pending) |

