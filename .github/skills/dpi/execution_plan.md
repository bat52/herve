# RISC-V DPI Co-Simulation — Detailed Execution Plan

> **Generated:** 2026-04-26
> **Purpose:** Step-by-step implementation guide for building features described in `dpi.md`
> **Status:** Core ISS complete (RV32I + M + C + CSR + IRQ); AHB BFM, IRQ examples, ISA validation runner all done; docs/performance tuning remaining; cross-simulator verification strategy defined
> **Last updated:** 2026-04-30 — Phase 8 added: cross-simulator verification (Icarus, ModelSim, four-tier strategy)

---

## Table of Contents

1. [Phase 1 — Core ISS Extensions](#phase-1--core-iss-extensions)
2. [Phase 2 — No-Library Dependency Enforcement](#phase-2--no-library-dependency-enforcement)
3. [Phase 3 — Verilator DPI Integration](#phase-3--verilator-dpi-integration)
4. [Phase 4 — Example Implementation](#phase-4--example-implementation)
5. [Phase 5 — ISA Support Comparison](#phase-5--isa-support-comparison)
6. [Phase 6 — RTL Coupling, IRQ](#phase-6--rtl-coupling-irq)
7. [Phase 7 — Performance Tuning](#phase-7--performance-tuning)
8. [Phase 8 — Cross-Simulator Verification](#phase-8--cross-simulator-verification)
9. [Dependency & Build Order](#dependency--build-order)

---

## Phase 1 — Core ISS Extensions

### Current State

The ISS (`rv32_dpi.c`) implements the **full RV32I base** instruction set. The following table shows what is implemented and what is missing.

**Already implemented in `execute_instruction()`:**

| Opcode | Instructions | Status |
|--------|-------------|--------|
| `0x33` (OP) | ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND | ✅ All |
| `0x13` (OP-IMM) | ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI | ✅ All |
| `0x03` (LOAD) | LB, LH, LW, LBU, LHU | ✅ All |
| `0x23` (STORE) | SB, SH, SW | ✅ All |
| `0x63` (BRANCH) | BEQ, BNE, BLT, BGE, BLTU, BGEU | ✅ All |
| `0x6f` (JAL) | JAL | ✅ |
| `0x67` (JALR) | JALR | ✅ |
| `0x37` (LUI) | LUI | ✅ |
| `0x17` (AUIPC) | AUIPC | ✅ |
| `0x73` (SYSTEM) | EBREAK, ECALL | ✅ Stops execution |

**Already implemented (since plan creation):**

| Feature | Status | Notes |
|---------|--------|-------|
| M-extension (MUL/DIV/REM) | ✅ Done | funct7=0x01 in opcode 0x33 |
| FENCE (opcode 0x0F) | ✅ Done | Handled as NOP |
| CSR instructions (CSRRW/CSRRS/CSRRC/CSRRWI/CSRRSI/CSRRCI) | ✅ Done | mstatus, mtvec, mepc, mcause, mvendorid, marchid, mhartid |
| MRET instruction | ✅ Done | Restores PC from mepc, sets MIE |
| IRQ polling in step loop | ✅ Done | Checked at start of each `rv_step()` call |
| Interrupt vectoring | ✅ Done | Vectored dispatch via mtvec + cause * 4 |
| `rv_init_from_buffer()` | ✅ Done | New API for embedded firmware |
| `run_fast` / `run_debug` Makefile targets | ✅ Done | Performance and debug variants |
| Updated `isa_support.md` | ✅ Done | Full matrix with checkboxes and gap analysis |
| C-extension (compressed) | ✅ Done | `execute_compressed()` in rv32_dpi.c (lines 161-481) — all quadrants 0/1/2 implemented |

**All Phase 1 items are completed.**

### 1.1 Add M-Extension

**File:** `dpi-riscv/sim/iss/rv32_dpi.c`

**Where to add:** Inside `execute_instruction()`, in the `case 0x33:` (OP) switch, add handling for `funct7 == 0x01`.

```c
// Inside case 0x33: OP switch
// funct7 == 0x01 => M-extension
if (funct7 == 0x01) {
    switch (funct3) {
        case 0x0: // MUL: rd = src1 * src2 (low 32 bits)
            write_reg(rd, (uint32_t)((int64_t)(int32_t)src1 * (int64_t)(int32_t)src2));
            break;
        case 0x1: // MULH: rd = (src1 * src2) >> 32 (signed*signed)
            write_reg(rd, (uint32_t)(((int64_t)(int32_t)src1 * (int64_t)(int32_t)src2) >> 32));
            break;
        case 0x2: // MULHSU: rd = (src1 * src2) >> 32 (signed*unsigned)
            write_reg(rd, (uint32_t)(((int64_t)(int32_t)src1 * (uint64_t)src2) >> 32));
            break;
        case 0x3: // MULHU: rd = (src1 * src2) >> 32 (unsigned*unsigned)
            write_reg(rd, (uint32_t)(((uint64_t)src1 * (uint64_t)src2) >> 32));
            break;
        case 0x4: // DIV: rd = src1 / src2 (signed)
            if (src2 == 0) { write_reg(rd, 0xFFFFFFFF); break; }
            if ((int32_t)src1 == INT32_MIN && (int32_t)src2 == -1) { write_reg(rd, INT32_MIN); break; }
            write_reg(rd, (uint32_t)((int32_t)src1 / (int32_t)src2));
            break;
        case 0x5: // DIVU: rd = src1 / src2 (unsigned)
            if (src2 == 0) { write_reg(rd, 0xFFFFFFFF); break; }
            write_reg(rd, src1 / src2);
            break;
        case 0x6: // REM: rd = src1 % src2 (signed)
            if (src2 == 0) { write_reg(rd, src1); break; }
            if ((int32_t)src1 == INT32_MIN && (int32_t)src2 == -1) { write_reg(rd, 0); break; }
            write_reg(rd, (uint32_t)((int32_t)src1 % (int32_t)src2));
            break;
        case 0x7: // REMU: rd = src1 % src2 (unsigned)
            if (src2 == 0) { write_reg(rd, src1); break; }
            write_reg(rd, src1 % src2);
            break;
    }
    pc = next_pc;
    return true;
}
```

**Verification:** Add a `test_mul_div()` function to the mmio_regs testbench that computes known products and checks register values.

### 1.2 Add C-Extension (Compressed Instructions)

**Architecture decision:** There are two approaches:

| Approach | Pros | Cons | Recommendation |
|----------|------|------|---------------|
| **Expand to RV32I** at runtime | Simpler, reuses existing decoder | Slightly slower decode | ✅ **Recommended for initial implementation** |
| Native C-format decoder | Faster | More code, more bugs | Defer to Phase 7 |

**Implementation plan for C-extension:**

1. **Detect 16-bit instructions:** In `rv_step()`, check `insn & 0x3` — if != 0x3, it's a compressed instruction.
2. **Pre-decode entry point:** Create a new function `bool execute_compressed(uint16_t insn)`.
3. **Map to equivalent RV32I instructions:** Each C instruction expands to 1-2 RV32I instructions.
4. **End of block:** `pc += 2` instead of `pc += 4`.

**Minimal C-extension subset (priority order):**

| Quadrant | Instructions | Priority | Notes |
|----------|-------------|----------|-------|
| **CI (quadrant 0)** | C.ADDI, C.ADDIW, C.LI, C.LUI, C.SLLI, C.NOP | P0 | Immediate loads, NOP |
| **CI (quadrant 0)** | C.JR, C.JALR, C.EBREAK | P0 | Control flow |
| **CIW (quadrant 0)** | C.ADDI4SPN | P0 | Stack pointer relative |
| **CL (quadrant 0)** | C.LW | P0 | Memory load (most used) |
| **CS (quadrant 0)** | C.SW | P0 | Memory store (most used) |
| **CB (quadrant 1)** | C.J, C.BNEZ, C.BEQZ | P1 | Branches and jumps |
| **CA (quadrant 1)** | C.MV, C.ADD, C.AND, C.OR, C.XOR, C.SUB | P1 | Register ops |
| **CI (quadrant 2)** | C.SRLI, C.SRAI, C.ANDI | P1 | Shifts and immediate AND |
| **CSS (quadrant 2)** | C.SWSP | P1 | Stack-relative store |
| **CIW (quadrant 2)** | C.LWSP | P1 | Stack-relative load |
| **Reserved (quadrant 3)** | N/A — all 32-bit | — | Fall through to existing decoder |

**Pseudocode for C-extension dispatch:**

```c
// In rv_step(), replace direct execute_instruction call:
uint32_t insn = read_u32(pc);
if ((insn & 0x3) != 0x3) {
    // 16-bit compressed instruction
    uint16_t c_insn = (uint16_t)(insn & 0xFFFF);
    if (!execute_compressed(c_insn)) {
        break;
    }
    pc += 2;
} else {
    if (!execute_instruction(insn)) {
        break;
    }
    pc += 4;
}
```

### 1.3 Add FENCE Handling

**File:** `dpi-riscv/sim/iss/rv32_dpi.c`

In `execute_instruction()`, add before the `default:` case:

```c
case 0x0F: // FENCE / FENCE.I
    // Both are no-ops in single-threaded ISS with no memory ordering
    break;
```

### 1.4 Add Minimal CSR Support

**Required CSR registers:**

| CSR Address | Name | Purpose |
|-------------|------|---------|
| `0x300` | `mstatus` | Machine status register (MIE bit) |
| `0x305` | `mtvec` | Trap vector base address |
| `0x341` | `mepc` | Exception PC |
| `0x342` | `mcause` | Exception cause |
| `0xF11` | `mvendorid` | Vendor ID (read-only) |
| `0xF12` | `marchid` | Architecture ID (read-only) |
| `0xF14` | `mhartid` | Hart ID (read-only) |

**Implementation:**

```c
// Add static CSR storage
static uint32_t csr_mstatus = 0;
static uint32_t csr_mtvec = 0;
static uint32_t csr_mepc = 0;
static uint32_t csr_mcause = 0;

// In execute_instruction(), case 0x73 (SYSTEM):
// funct3 distinguishes CSR access:
//   0x0 = ECALL, EBREAK (already handled)
//   0x1 = CSRRW
//   0x2 = CSRRS
//   0x3 = CSRRC
//   0x5 = CSRRWI
//   0x6 = CSRRSI
//   0x7 = CSRRCI
if (funct3 != 0) {
    uint32_t csr_addr = (insn >> 20) & 0xFFF;
    // Implement CSRRW/CSRRS/CSRRC with switch on csr_addr
    switch (csr_addr) {
        case 0x300: // mstatus
        case 0x305: // mtvec
        case 0x341: // mepc
        case 0x342: // mcause
        // ...
    }
}
```

**Important caveat:** The spec describes CSR access as a single instruction that reads the old value and writes the new value in one operation. For CSRRS (read + set bits), the implementation must do: `old = CSR; CSR |= rs1; rd = old`.

### 1.5 Wire IRQ Polling into `rv_step()`

**Current behavior:** `rv_set_irq(mask)` stores the mask, but `rv_step()` never reads it.

**Proposed change in `rv_step()`:**

```c
int rv_step(int max_instructions) {
    if (!initialized || max_instructions <= 0) return 0;

    // Check for pending interrupts at start of step batch
    if (irq_mask != 0) {
        // Simplified trap handling: jump to mtvec + (cause * 4)
        uint32_t cause = __builtin_ctz(irq_mask); // or custom bit-scan
        csr_mcause = cause;
        csr_mepc = pc;
        // Clear the bit we're servicing
        irq_mask &= ~(1u << cause);
        // Jump to handler
        pc = csr_mtvec + cause * 4; // or mtvec with vectored mode
        return 1;
    }

    int executed = 0;
    for (int i = 0; i < max_instructions; ++i) {
        // ... existing execution ...
    }
    return executed;
}
```

**Note:** A full trap handler needs `csr_mstatus.MIE` management (interrupt enable/disable), which requires CSR support (section 1.4) to be implemented first.

---

## Phase 2 — No-Library Dependency Enforcement

### Current State

**Already compliant.** The ISS (`rv32_dpi.c`) uses only:

| Header | Functions Used | Acceptable? |
|--------|---------------|-------------|
| `<stdint.h>` | uint8_t, uint32_t, uint64_t, etc. | ✅ Standard C |
| `<stdbool.h>` | bool, true, false | ✅ Standard C |
| `<stdlib.h>` | malloc, free, NULL | ✅ Standard C |
| `<string.h>` | memset, memcpy | ✅ Standard C |
| `<stdio.h>` | FILE, fopen, fread, fclose, printf, fprintf | ✅ Standard C (available in Verilator context) |

### Enforcement Checklist

- [x] No POSIX-specific headers (<unistd.h>, <fcntl.h>, <sys/...>)
- [x] No external libraries (no libelf, no libriscv, no dlopen)
- [x] No C++ runtime features in ISS code (ISS is pure C)
- [x] No dynamic loading, no dlopen/dlsym
- [x] No pthread or multithreading
- [x] No floating-point except in testbench code
- [x] No C99 variable-length arrays in ISS core (all fixed-size: `regs[32]`)

### Firmware Loader Refinement

The current `rv_init()` uses `fopen`/`fread` from `<stdio.h>`. This is **acceptable** for Verilator DPI because:

1. Verilator links against the host C library.
2. The function is called from C++ testbench context before simulation starts.
3. No RTL timing dependency.

**Optional improvement:** Add a `rv_init_from_buffer(const uint8_t *data, size_t size)` for cases where firmware is embedded in the testbench binary rather than loaded from a file. This would eliminate the `<stdio.h>` dependency if needed for extreme bare-metal scenarios.

### Dependency Policy Documentation

Add to each ISS file header:

```c
/*
 * Dependency Policy:
 * This file must compile with only standard C headers available
 * in Verilator's DPI compilation environment.
 * Permitted headers: <stdint.h>, <stdbool.h>, <stddef.h>,
 *                    <stdlib.h>, <string.h>, <stdio.h>
 * No libc features beyond malloc/free/memset/memcpy/fopen/fread.
 * No external libraries, no POSIX APIs.
 */
```

---

## Phase 3 — Verilator DPI Integration

### Current State

✅ **DPI imports from SV → C:** Already in `tb_top.sv`:

```systemverilog
import "DPI-C" function void tb_load_firmware(string firmware_path);
```

✅ **DPI exports from C → SV:** Already in `tb_top.sv`:

```systemverilog
export "DPI-C" function dpi_mmio_read;
export "DPI-C" function dpi_mmio_write;
```

✅ **C-side DPI extern declarations:** Already in `rv32_dpi.c`:

```c
extern uint32_t dpi_mmio_read(uint32_t addr);
extern void dpi_mmio_write(uint32_t addr, uint32_t value);
```

✅ **Verilator harness with direct model access:** Already in `rv32_dpi_tb.cpp`, the `dpi_mmio_read`/`dpi_mmio_write` functions bypass Verilator's DPI export dispatch and access the model's signals directly.

✅ **Build system with symbol conflict avoidance:** Already in `Makefile`, the `-DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_` flags suppress Verilator's auto-generated DPI stubs, avoiding duplicate symbol errors.

### Enhancement Opportunities

#### 3.1 Add DPI Import for ISS Control from SV

Currently the SV testbench can't call `rv_step()` or `rv_init()` directly — this is done from C++. If we want the SV testbench to control the simulation schedule, add:

```systemverilog
// In tb_top.sv or a wrapper module
import "DPI-C" function int  rv_step(int max_insn);
import "DPI-C" function void rv_init(string fw, int ram_sz);
import "DPI-C" function void rv_reset(int pc);
import "DPI-C" function void rv_set_irq(int mask);
import "DPI-C" function longint rv_get_ram();
import "DPI-C" function int  rv_get_pc();
```

**Downside:** Calling `rv_step()` from SV will trigger the C function during `eval()`, which may break Verilator's scheduling. **Recommendation:** Keep the control loop in C++ (the testbench) and only use the C++ DPI export wrappers for MMIO callbacks.

#### 3.2 Add Scope Management

When DPI exports are called from pure C (outside Verilator's eval), the scope may be invalid. The current approach sidesteps this by using direct model access. For a more portable solution, use `svSetScope`:

```c
extern "C" int dpi_mmio_read(int addr) {
    static svScope scope = NULL;
    if (scope == NULL) {
        scope = svGetScopeFromName("tb_top");
    }
    svSetScope(scope);
    // Now we can use Verilator's DPI export dispatch
}
```

**Decision:** The direct-model-access approach is simpler and sufficient for Verilator. Reserve `svSetScope` for when cadence irun (NCsim) support is needed.

#### 3.3 Add Fatal Error Handling for MMIO Timeout

If the RTL takes too long to respond to an MMIO access, the simulation deadlocks. Add a timeout mechanism:

```c
extern "C" int dpi_mmio_read(int addr) {
    // Retry with timeout
    for (int retry = 0; retry < 1000; retry++) {
        int result = direct_model_read(addr);
        if (/* ready signal is asserted */) {
            return result;
        }
        // Yield to RTL simulation
        // This requires calling eval() from here, which is recursive.
        // Better approach: use a handshake state machine.
    }
    fprintf(stderr, "FATAL: MMIO read timeout at 0x%08x\n", addr);
    exit(1);
}
```

**Caveat:** Calling `tb->eval()` from inside a DPI callback creates a **recursive eval**, which Verilator does not support. The proper solution is a **two-phase handshake** (see Phase 6).

---

## Phase 4 — Example Implementation

### Current State

| Example | File(s) | Status |
|---------|---------|--------|
| Hello world / MMIO write | `firmware.S` + `tb_top.sv` + `rv32_dpi_tb.cpp` | ✅ Done |
| Config register read/write | `tb_top_mmio_regs.sv` + `rv32_dpi_mmio_regs_tb.cpp` | ✅ Done |
| Interrupt-driven GPIO toggle | `firmware_irq.S` + `tb_top.sv` + `rv32_dpi_irq_tb.cpp` | ✅ Done |
| RTL DUT with AHB-Lite BFM | `firmware_ahb.S` + `tb_top_ahb.sv` + `ahb_lite_bfm.sv` + `ahb_gpio.sv` + `rv32_dpi_ahb_tb.cpp` | ✅ Done |

**All Phase 4 examples are implemented.**

### 4.1 Interrupt-Driven GPIO Toggle Example (Reference)

**Purpose:** Demonstrate IRQ injection from SV → ISS, and the ISS interrupt handler toggling a GPIO output.

**MMIO register map for GPIO:**

| Address | Name | Width | Access | Description |
|---------|------|-------|--------|-------------|
| `0x1000_0000` | `GPIO_OUT` | 32 | R/W | Output value (bit 0 = LED) |
| `0x1000_0004` | `GPIO_IE` | 32 | R/W | Interrupt enable |
| `0x1000_0008` | `GPIO_STATUS` | 32 | R | Interrupt status |

**Firmware flow (`firmware_irq.S`):**

```asm
_start:
    // 1. Configure mtvec to point to interrupt handler
    lui t0, 0x00001          // handler at ~0x1000 (actually 0x100 from LUI)
    csrw mtvec, t0

    // 2. Enable interrupts in mstatus
    li t0, 0x8               // MIE bit
    csrs mstatus, t0

    // 3. Configure GPIO: enable interrupt
    lui t0, 0x10000          // MMIO base
    addi t1, x0, 1
    sw t1, 4(t0)             // GPIO_IE = 1

    // 4. Main loop: just WFI (wait for interrupt)
main_loop:
    wfi
    j main_loop

    // Redirect execution flow:
    // When interrupt fires, PC jumps to handler vector.

interrupt_handler:
    // Read GPIO_STATUS to clear IRQ
    lw t1, 8(t0)
    // Toggle GPIO output
    lw t2, 0(t0)
    xori t2, t2, 1
    sw t2, 0(t0)
    // Return from interrupt
    mret
```

**SV testbench for IRQ:**

```systemverilog
initial begin
    // Run firmware for a bit
    rv_step(100);
    #100ns;
    // Inject interrupt
    rv_set_irq(32'h0000_0001);
    #10ns;
    // Run firmware — should handle interrupt
    rv_step(100);
    // Check GPIO output has toggled
    $display("GPIO_OUT = 0x%08x", dpi_mmio_read(32'h1000_0000));
end
```

### 4.2 RTL DUT with AHB-Lite BFM (Reference)

**Purpose:** Move from signal-level MMIO to a proper bus protocol that can be reused with real RTL designs.

**AHB-Lite signals:**

```systemverilog
module ahb_lite_master (
    input  wire        HCLK,
    input  wire        HRESETn,
    // Master interface
    output reg  [31:0] HADDR,
    output reg  [1:0]  HTRANS,   // 00=IDLE, 10=NONSEQ
    output reg         HWRITE,
    output reg  [2:0]  HSIZE,
    output reg  [31:0] HWDATA,
    input  wire        HREADY,
    input  wire [31:0] HRDATA
);
```

**BFM functions:**

```systemverilog
function automatic int ahb_read(input [31:0] addr);
    HADDR   = addr;
    HTRANS  = 2'b10;  // NONSEQ
    HWRITE  = 1'b0;
    HSIZE   = 3'b010; // word (4 bytes)
    @(posedge HCLK);
    while (!HREADY) @(posedge HCLK);
    return HRDATA;
endfunction

function automatic void ahb_write(input [31:0] addr, input [31:0] data);
    HADDR   = addr;
    HTRANS  = 2'b10;  // NONSEQ
    HWRITE  = 1'b1;
    HSIZE   = 3'b010;
    HWDATA  = data;
    @(posedge HCLK);
    while (!HREADY) @(posedge HCLK);
endfunction
```

**DPI wiring:**

```systemverilog
function int dpi_mmio_read(int addr);
    return ahb_read(addr);
endfunction

function void dpi_mmio_write(int addr, int data);
    ahb_write(addr, data);
endfunction
```

---

## Phase 5 — ISA Support Comparison

### Current State

✅ **Infrastructure complete.** `run_riscv_tests.sh` (available via `make run_riscv_tests`) clones, builds, and runs riscv-tests against the ISS using `rv32_dpi_riscv_tests.cpp`. The test runner checks gp (x3) for pass/fail per the riscv-tests convention.

`isa_support.md` exists with a full matrix — checkboxes should be updated after running the suite to capture actual results.

### 5.1 Reference Testbench Selection

| Testbench | Language | Coverage | License | Recommendation |
|-----------|----------|----------|---------|---------------|
| [riscv-tests](https://github.com/riscv-software-src/riscv-tests) | Assembly + macros | RV32I/U/M/C, privileged | BSD | ✅ **Primary reference** |
| [riscv-arch-test](https://github.com/riscv-non-isa/riscv-arch-test) | Assembly + YAML + SAIL | Full ISA coverage | BSD | Use for formal validation |
| [riscv-formal](https://github.com/YosysHQ/riscv-formal) | SystemVerilog | Bounded verification | ISC | Overkill for DPI co-sim |

### 5.2 Updated ISA Support Matrix

**File:** `dpi-riscv/docs/isa_support.md` (to be updated with actual checkbox states)

After implementing Phase 1, the matrix should show:

#### RV32I Base (should be fully green after Phase 1.1-1.4)

- [x] LUI, AUIPC
- [x] JAL, JALR
- [x] BEQ, BNE, BLT, BGE, BLTU, BGEU
- [x] LB, LH, LW, LBU, LHU
- [x] SB, SH, SW
- [x] ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI
- [x] ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND
- [x] FENCE, ECALL, EBREAK (FENCE = NOP)

#### RV32M (to be added in Phase 1.1)

- [x] MUL, MULH, MULHSU, MULHU
- [x] DIV, DIVU, REM, REMU

#### RV32C (to be added in Phase 1.2)

- [x] C.ADDI, C.LI, C.LUI, C.NOP
- [x] C.JR, C.JALR, C.EBREAK
- [x] C.LW, C.SW, C.LWSP, C.SWSP
- [x] C.J, C.BNEZ, C.BEQZ
- [x] C.MV, C.ADD

#### Zicsr (to be added in Phase 1.4)

- [x] CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI
- [x] MRET, ECALL, EBREAK

### 5.3 Validation Strategy

1. **Download riscv-tests** and compile with `riscv64-unknown-elf-gcc`.
2. **Extract individual test binaries** for each instruction.
3. **Create a test runner** in C that:
   - Initializes ISS with each test binary
   - Runs until EBREAK
   - Checks `regs[3]` (gp) for test pass/fail (riscv-tests convention: gp = 1 on pass)
4. **Report pass/fail per instruction.**

```bash
# Example riscv-tests workflow
git clone https://github.com/riscv-software-src/riscv-tests
cd riscv-tests/isa
make RISCV_PREFIX=riscv64-unknown-elf- \
     rv32ui-p-add rv32ui-p-addi rv32ui-p-lw ...
```

```c
// Test runner pseudocode
int run_test(const char *binary) {
    rv_init(binary, 1 << 20);
    rv_reset(0);
    int steps = 0;
    while (steps < 10000) {
        int exec = rv_step(1000);
        if (exec == 0) break; // hit EBREAK
        steps += exec;
    }
    uint32_t gp = regs[3];
    printf("%s: %s\n", binary, (gp == 1) ? "PASS" : "FAIL");
    return (gp == 1) ? 0 : 1;
}
```

### 5.4 Gap Documentation

For any failing instructions, document:

```
### Instruction: `fsw` (floating-point store)
- **Category:** RV32F (single-precision float)
- **Expected behavior:** Store 32-bit float to memory
- **Current behavior:** Not supported (no FPU in ISS)
- **Priority:** Low — FP not needed for bare-metal firmware
- **Mitigation:** Compile firmware with `-march=rv32imc` (no float)
```

---

## Phase 6 — RTL Coupling, IRQ

### Current State

| Feature | Status | Notes |
|---------|--------|-------|
| AHB BFM | ✅ Done | `ahb_lite_bfm.sv` with blocking read/write tasks |
| MMIO connected to DUT | ✅ Done | `tb_top_ahb.sv` + `ahb_gpio.sv` provide full AHB DUT |
| IRQ from RTL to ISS | ✅ Done | `rv_set_irq` wired into `rv_step()` with vectored dispatch |
| Shared RAM | ✅ Working | Pointer returned by `rv_get_ram()` |

### 6.1 AHB-Lite BFM Implementation

**File:** `dpi-riscv/sim/bus/ahb_lite_bfm.sv`

```systemverilog
/*
 * AHB-Lite Bus Functional Model
 *
 * Provides blocking read/write functions callable from DPI.
 * Implements the handshake protocol (HTRANS, HREADY).
 *
 * Usage (from DPI-C):
 *   int data = ahb_read(0x1000_0000);
 *   ahb_write(0x1000_0004, 0xDEAD_BEEF);
 */

module ahb_lite_bfm (
    input  wire        HCLK,
    input  wire        HRESETn,
    output reg  [31:0] HADDR,
    output reg  [1:0]  HTRANS,
    output reg         HWRITE,
    output reg  [2:0]  HSIZE,
    output reg  [31:0] HWDATA,
    input  wire        HREADY,
    input  wire [31:0] HRDATA
);

    // Defaults: IDLE, no transaction
    initial begin
        HADDR   = 32'h0;
        HTRANS  = 2'b00;  // IDLE
        HWRITE  = 1'b0;
        HSIZE   = 3'b000;
        HWDATA  = 32'h0;
    end

    task automatic ahb_read_task(input [31:0] addr, output [31:0] data);
        @(posedge HCLK);
        HADDR   <= addr;
        HTRANS  <= 2'b10;  // NONSEQ
        HWRITE  <= 1'b0;
        HSIZE   <= 3'b010;
        @(posedge HCLK);
        while (!HREADY) @(posedge HCLK);
        data = HRDATA;
        HTRANS <= 2'b00;   // back to IDLE
    endtask

    task automatic ahb_write_task(input [31:0] addr, input [31:0] data);
        @(posedge HCLK);
        HADDR   <= addr;
        HTRANS  <= 2'b10;  // NONSEQ
        HWRITE  <= 1'b1;
        HSIZE   <= 3'b010;
        HWDATA  <= data;
        @(posedge HCLK);
        while (!HREADY) @(posedge HCLK);
        HTRANS  <= 2'b00;  // back to IDLE
    endtask
endmodule
```

### 6.2 DPI → AHB Bridge

**File:** `dpi-riscv/sim/harness/tb_top_ahb.sv`

```systemverilog
module tb_top_ahb (
    input  wire       clk,
    input  wire       rstn,
    // AHB interface to DUT
    output reg  [31:0] haddr,
    output reg  [1:0]  htrans,
    output reg         hwrite,
    output reg  [2:0]  hsize,
    output reg  [31:0] hwdata,
    input  wire        hready,
    input  wire [31:0] hrdata
);

    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    function int dpi_mmio_read(int addr);
        reg [31:0] data;
        ahb_master.ahb_read_task(addr, data);
        return int'(data);
    endfunction

    function void dpi_mmio_write(int addr, int data);
        ahb_master.ahb_write_task(addr, data);
    endfunction

    ahb_lite_bfm ahb_master (
        .HCLK(clk),
        .HRESETn(rstn),
        .HADDR(haddr),
        .HTRANS(htrans),
        .HWRITE(hwrite),
        .HSIZE(hsize),
        .HWDATA(hwdata),
        .HREADY(hready),
        .HRDATA(hrdata)
    );
endmodule
```

### 6.3 IRQ Injection Path

The IRQ injection path is now fully implemented:

1. **SV side:** Testbench calls `rv_set_irq` via DPI import.
2. **ISS side:** `rv_step()` checks `irq_mask` at start of each batch, performs vectored dispatch (mtvec + cause * 4), saves mepc, and clears MIE in mstatus.
3. **Synchronization points:** Start of each `rv_step()` call, plus after MMIO writes that may trigger RTL IRQ.

**Timing diagram:**

```
Time:   |---ISS batch---|---RTL ticks---|---ISS batch---|---RTL ticks---|
       rv_step(1000)   toggle_clk()   rv_step(1000)   toggle_clk()
                              |
                         rtl_assert_irq()
                              ↓
                         rv_set_irq(1)  → checked at next rv_step
```

---

## Phase 7 — Performance Tuning

### 7.1 Batch Size Optimization

**Goal:** Find the optimal number of instructions per `rv_step()` call.

**Methodology:**

```c
// Performance test in testbench
for (int batch = 1; batch <= 100000; batch *= 10) {
    uint64_t start = rdcycle(); // or clock_gettime
    int total = 0;
    int total_steps = 0;
    while (total < 1000000) {
        total += rv_step(batch);
        total_steps++;
        // Simulate RTL time: 10 clock ticks per batch
        for (int i = 0; i < 10; i++) tick(tfp);
    }
    uint64_t end = rdcycle();
    printf("batch=%d total_insn=%d steps=%d time=%lu cycles\n",
           batch, total, total_steps, end - start);
}
```

**Expected results:** Larger batches reduce DPI crossing overhead, but too-large batches (e.g., >100k) may cause cache misses or lack of synchronization responsiveness.

**Recommended default:** 1000 instructions per batch (as specified in `dpi.md`).

### 7.2 Minimize DPI Crossings

**Current state:** Each MMIO access causes a DPI crossing (C → SV). For bursty MMIO traffic, this can dominate simulation time.

**Optimization strategies:**

| Strategy | Description | Impact | Complexity |
|----------|-------------|--------|------------|
| Merge MMIO writes | Buffer writes, flush at batch end | High | Medium |
| Read caching | Cache MMIO reads within a batch | Medium | Low |
| Direct RAM access (already done) | Shared RAM pointer avoids DPI for memory ops | ✅ Already done | — |

**Implementation of write merging:**

```c
// In rv32_dpi.c
#define MMIO_BUFFER_SIZE 16
static struct {
    uint32_t addr;
    uint32_t data;
    bool     pending;
} mmio_write_buffer[MMIO_BUFFER_SIZE];
static int mmio_write_count = 0;

static void flush_mmio_writes(void) {
    for (int i = 0; i < mmio_write_count; i++) {
        dpi_mmio_write(mmio_write_buffer[i].addr, mmio_write_buffer[i].data);
    }
    mmio_write_count = 0;
}

static void write_u32(uint32_t addr, uint32_t value) {
    if (is_mmio_address(addr)) {
        // Buffer the write instead of calling DPI immediately
        if (mmio_write_count < MMIO_BUFFER_SIZE) {
            mmio_write_buffer[mmio_write_count].addr = addr;
            mmio_write_buffer[mmio_write_count].data = value;
            mmio_write_buffer[mmio_write_count].pending = true;
            mmio_write_count++;
        }
        return;
    }
    // ... normal RAM write ...
}
```

**Flush points:** End of `rv_step()`, before MMIO reads, before IRQ check.

### 7.3 Waveform Dumping Strategy

| Dump mode | When to use | Implementation |
|-----------|-------------|---------------|
| **Full trace** | Debugging failures | `--trace` flag (current behavior) |
| **No dump** | Performance runs | Default (change Makefile to not pass `--trace`) |
| **Dump on fail** | CI / regression | Dump last N cycles before EBREAK/failure |

**Makefile targets for performance:**

```makefile
run_fast: obj_dir/V$(TOP)
    ./obj_dir/V$(TOP) --no-trace 2>/dev/null

run_debug: obj_dir/V$(TOP)
    ./obj_dir/V$(TOP) --trace  # full VCD dump
```

---

## Phase 8 — Cross-Simulator Verification

### Motivation

The project currently verifies the ISS exclusively with **Verilator**. While Verilator's DPI-C support is excellent for compiled simulation, verifying the ISS SV integration with other simulators improves confidence that the DPI patterns used are standard-compliant and identifies simulator-specific bugs early.

### 8.1 Simulator Comparison

| Simulator | DPI Support | License | Ubuntu Package | Ease of Setup | Recommendation |
|-----------|------------|---------|----------------|---------------|----------------|
| **Verilator** (v5.020) | ✅ Full DPI-C import/export | GPL/LGPL | `apt install verilator` | ✅ Installed | **Primary target** (already works) |
| **Icarus Verilog** (v12.0) | ⚠️ Partial DPI-C (import, export via `-g2005-sv`) | GPL | `apt install iverilog` | ✅ One command | **Best free alternative** |
| **GTKWave** (v3.3) | N/A (viewer only) | GPL | `apt install gtkwave` | ✅ One command | Waveform viewer only |
| **ModelSim Intel FPGA Starter** | ✅ Full DPI-C (IEEE 1800) | Free (registration req.) | Manual install | ⚠️ ~3GB, needs Quartus | **Gold standard** but heavy |
| **Questa** (Intel/AMD) | ✅ Full DPI-C | Commercial | N/A | ❌ License req. | Not viable for CI |
| **Xsim** (Vivado) | ✅ DPI-C support | Free (with Vivado) | Manual install | ⚠️ ~20GB | Overkill for this project |
| **ncsim/irun** (Cadence) | ✅ Full DPI-C | Commercial | N/A | ❌ License req. | Not viable for CI |
| **VCS** (Synopsys) | ✅ Full DPI-C | Commercial | N/A | ❌ License req. | Not viable for CI |
| **CVC** (Cver) | ❌ No DPI-C support | Free | `apt install verilator` | ✅ | Obsolete, not recommended |

**Conclusion: Icarus Verilog is the only practical free alternative.** It is available in the default Ubuntu repos, installs with a single `apt` command, and supports DPI-C (though with some limitations compared to Verilator).

### 8.2 Icarus Verilog DPI Support — Detailed Analysis

#### 8.2.1 What Icarus Supports

Icarus Verilog (v12.0, `-g2005-sv` flag) supports:

- **DPI-C imports** — Calling C functions from Verilog (`import "DPI-C" function ...`)
- **DPI-C exports** — Calling Verilog functions from C (`export "DPI-C" function ...`) via VPI
- **`svSetScope` / `svGetScopeFromName`** — Scope management (same API as Verilator)
- **`$dumpvars` / `$dumpfile`** — VCD generation (with `-g2005-sv` or `-g2012`)

#### 8.2.2 Key Differences from Verilator

| Aspect | Verilator | Icarus Verilog |
|--------|-----------|----------------|
| **Execution model** | Compiled C++ translation (cycle-accurate) | Event-driven (interpreted via vvp) |
| **DPI export linkage** | Direct C++ function calls in compiled model | Uses VPI shared library (`.vpi` / `.so`) loaded with `-m` flag |
| **`@(posedge clk)` inside DPI exports** | ❌ Not supported (recursive eval) | ✅ Supported (native event scheduling) |
| **`--timing` flag needed** | ✅ for timing controls | ❌ Native |
| **`string` type in DPI** | ⚠️ Supported via `const char*` | ⚠️ Supported via `const char*` |
| **SystemVerilog features** | Broad support (SV 2017 mostly) | Basic SV (SV 2005 subset) |
| **`export "DPI-C" function` from portless module** | ✅ Supported | ⚠️ Module must have at least one port or be wrapped |
| **Build flow** | `verilator --cc --exe --build` produces binary directly | `iverilog -o sim.vvp` + `vvp -M. -m module sim.vvp` two-step |

#### 8.2.3 DPI Export Mechanics in Icarus

Icarus does **not** allow Verilog DPI exports to be called directly from C code as regular functions. Instead, it uses the **VPI (Verilog Procedural Interface)** mechanism:

1. Compile Verilog: `iverilog -g2005-sv -o sim.vvp tb_top.sv`
2. Compile C code into a shared library: `gcc -shared -o dpi_mod.vpi -I/path/to/vvp dpi_wrapper.c rv32_dpi.c`
3. Run: `vvp -M. -m dpi_mod sim.vvp`

The `.vpi` module is loaded as a shared library, and Icarus resolves DPI export symbols from it. A `register()` function in the `.vpi` module tells Icarus where to find each DPI export.

#### 8.2.4 Architectural Impact

The current C++ testbenches (`rv32_dpi_tb.cpp`) use **direct model access**:

```cpp
// Direct model access — works ONLY with Verilator
extern "C" int dpi_mmio_read(int addr) {
    return tb->mem_read;  // Vtb_top pointer
}
```

For Icarus, the DPI callbacks must be written as **pure C functions** (no C++ model access) that use the VPI/scope mechanism to interact with SV signals. This means the same `dpi_mmio_read`/`dpi_mmio_write` functions take different approaches depending on the simulator.

**Proposed architecture for simulator abstraction:**

```
                    +-------------------+
                    |   rv32_dpi.c       |  (pure C, simulator-agnostic)
                    |   ISS core         |
                    +--------+----------+
                             |
                    dpi_mmio_read / write calls
                             |
                    +--------v----------+
                    | dpi_bridge layer   |  (one implementation per simulator)
                    +--------+----------+
                             |
              +--------------+--------------+
              |              |              |
     Verilator bridge   Icarus bridge    ModelSim bridge
     (direct model      (VPI shared      (svSetScope +
      access)            library)         standard DPI)
```

### 8.3 Icarus Verilog — Implementation Roadmap

#### Phase 8.3.1: Prerequisites

```bash
# Install Icarus Verilog
sudo apt install iverilog

# Verify installation
iverilog -V
# Should show: Icarus Verilog version 12.0 (stable) or later

# Install VPI development headers (needed for DPI exports)
dpkg -L iverilog | grep vpi_user.h
# Should show: /usr/include/iverilog/vpi_user.h
```

#### Phase 8.3.2: Minimal Icarus Smoke Test (No DPI)

**Goal:** Verify Icarus can parse the existing SV files without modification.

```bash
# Test basic SV parsing
cd dpi-riscv
iverilog -g2005-sv -o /tmp/test_syntax.vvp sim/harness/tb_top.sv 2>&1 || true
```

**Expected issues to resolve:**
- Icarus may error on SystemVerilog constructs that Verilator accepts.
- `export "DPI-C"` inside a module with no ports might need wrapping.
- The `always @(*)` combinational blocks are fine (Icarus supports them).
- The `typedef enum logic [1:0]` in `ahb_lite_bfm.sv` may need `-g2012` flag.

**Mitigation:** Create an Icarus-compatible wrapper for `tb_top_mmio_regs.sv` (which has no ports and is simplest — only 35 lines). This is the easiest testbench to port first.

#### Phase 8.3.3: Implement Icarus DPI Bridge

**File:** `dpi-riscv/sim/icarus/icarus_dpi_bridge.c`

```c
/*
 * Icarus DPI bridge — implements dpi_mmio_read / dpi_mmio_write
 * using VPI to drive signal values instead of direct model access.
 *
 * Compile as a shared library:
 *   gcc -shared -o icarus_dpi_bridge.vpi -I/usr/include/iverilog \
 *       icarus_dpi_bridge.c ../iss/rv32_dpi.c
 *
 * Run:
 *   iverilog -g2005-sv -o sim.vvp tb_top_mmio_regs.sv
 *   vvp -M. -m icarus_dpi_bridge sim.vvp
 */

#include <vpi_user.h>
#include <stdio.h>
#include <stdint.h>
#include "rv32_dpi.h"

// Forward declarations of DPI export implementations
extern uint32_t dpi_mmio_read(uint32_t addr);
extern void dpi_mmio_write(uint32_t addr, uint32_t value);

// VPI handles for SV signals (cached for performance)
static vpiHandle hw_cfg_reg_handle = NULL;

static void init_vpi_handles(void) {
    if (hw_cfg_reg_handle != NULL) return;

    vpiHandle top = vpi_handle_by_name((PLI_BYTE8*)"tb_top_mmio_regs", NULL);
    if (top == NULL) {
        fprintf(stderr, "ERROR: Cannot find tb_top_mmio_regs module\n");
        return;
    }
    // Cache signal handles for efficient MMIO callbacks
}

// DPI export implementations as VPI callbacks
PLI_INT32 dpi_mmio_read_cb(PLI_BYTE8 *user_data) {
    // ... VPI-based read implementation
    return 0;
}

// Registration function — called by vvp when shared library is loaded
void (*vlog_startup_routines[])() = {
    register_dpi_bridge,
    0
};
```

**Note:** The VPI approach is more complex than Verilator's direct model access. An alternative simpler approach for Icarus is to write a **pure-SV testbench** that uses DPI imports to call `rv_step()`, `rv_init()`, etc. directly from the SV side, avoiding the need for C-side signal access altogether:

```systemverilog
// Pure-SV testbench using DPI imports (simpler Icarus approach)
module tb_icarus;
    import "DPI-C" function void rv_init(string firmware, int ram_size);
    import "DPI-C" function void rv_reset(int pc);
    import "DPI-C" function int  rv_step(int max_insn);
    import "DPI-C" function int  rv_get_reg(int reg);

    // SV-side MMIO registers
    reg [31:0] mmio_regs [0:63];

    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    function int dpi_mmio_read(int addr);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0100) begin
            dpi_mmio_read = mmio_regs[(addr - 32'h1000_0000) / 4];
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0100) begin
            mmio_regs[(addr - 32'h1000_0000) / 4] = data;
        end
    endfunction

    initial begin
        $dumpfile("tb_icarus.vcd");
        $dumpvars(0, tb_icarus);

        // Load firmware into ISS
        rv_init("firmware.bin", 1 << 20);
        rv_reset(0);

        // Execute firmware
        int insn = rv_step(100);
        $display("Executed %0d instructions", insn);
        $display("MMIO[0] = 0x%08x", mmio_regs[0]);

        $finish;
    end
endmodule
```

**This pure-SV approach is recommended for Icarus.** It avoids the complexity of VPI shared libraries entirely, and works with Icarus's native DPI-C import/export capabilities. The downside is it cannot drive RTL clocks or instantiate RTL DUTs — but it's sufficient for verifying the ISS→DPI→SV→MMIO round-trip.

#### Phase 8.3.4: Icarus Test Targets

Add to `dpi-riscv/Makefile`:

```makefile
# ---- Icarus Verilog test targets ----

ICARUS ?= iverilog
ICARUS_FLAGS = -g2005-sv

# Pure-SV smoke test with Icarus
obj_dir/tb_icarus.vvp: sim/iss/rv32_dpi.c sim/iss/rv32_dpi.h tests/tb_icarus.sv
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -shared -fPIC -o obj_dir/rv32_dpi_icarus.so \
	      -I$(CURDIR)/sim/iss $(C) -I/usr/include/iverilog
	$(ICARUS) $(ICARUS_FLAGS) -o $@ tests/tb_icarus.sv

run_icarus: obj_dir/tb_icarus.vvp $(FIRMWARE_BIN)
	vvp -Mobj_dir -mrv32_dpi_icarus $<
```

### 8.4 ModelSim / Questa

#### 8.4.1 Assessment

ModelSim and Questa (Intel/AMD versions) are the industry-standard Verilog simulators with full IEEE 1800-2017 DPI-C compliance. However:

| Version | License | Availability | Verdict |
|---------|---------|-------------|---------|
| **ModelSim Intel FPGA Starter Edition** | Free (web registration) | Requires Quartus Prime Lite download (~3GB) | ✅ Feasible but heavy for CI |
| **Questa Intel FPGA Edition** | Free (with Quartus) | Same download | Same as above |
| **ModelSim SE / DE** | Commercial | License server required | ❌ Not for CI |
| **Questa Advanced Simulator** | Commercial | License server required | ❌ Not for CI |

#### 8.4.2 DPI Compatibility

ModelSim/Questa would work with the **current SV code almost verbatim** because:

1. The `export "DPI-C" function dpi_mmio_read/write` declarations are IEEE 1800 standard.
2. The DPI import `rv_set_irq()` is standard.
3. The `ahb_lite_bfm.sv` uses `@(posedge clk)` in tasks — works natively in event-driven simulators.
4. The auto-generated DPI stubs (that Verilator normally creates and we suppress) are not needed.

**The only change needed** for ModelSim would be proper scope management in the C++ testbench (as described in Phase 3.2). ModelSim's `-dpiheader` flag can generate C headers for the DPI exports automatically.

#### 8.4.3 Practical Recommendation

Given the installation complexity (~3GB download), ModelSim is **not recommended for CI pipelines** unless the project already has access to it. Reserve ModelSim verification for:

- Pre-release validation of DPI portability
- Debugging DPI scope issues that are simulator-specific (e.g., `svSetScope` behavior)
- Formal compliance checking when the SV code uses more advanced SV features

### 8.5 Verification Strategy — Four Tiers

```
                    ┌─────────────────────────────────────┐
                    │  Tier 1: Standalone ISS             │
                    │  (no simulator needed)               │
                    │                                     │
                    │  • make run_standalone               │
                    │  • make run_c_test                   │
                    │  • make run_riscv_tests              │
                    │  • make run_irq_standalone           │
                    │                                     │
                    │  Run on every commit                 │
                    └──────────┬──────────────────────────┘
                               │
                    ┌──────────v──────────────────────────┐
                    │  Tier 2: Verilator                  │
                    │  (primary SV co-sim target)          │
                    │                                     │
                    │  • make run (or run_fast/run_debug)  │
                    │  • make run_mmio_regs                │
                    │  • make run_muldiv                   │
                    │  • make run_irq                      │
                    │  • make run_ahb                      │
                    │                                     │
                    │  Run on every commit                 │
                    └──────────┬──────────────────────────┘
                               │
                    ┌──────────v──────────────────────────┐
                    │  Tier 3: Icarus Verilog             │
                    │  (DPI compatibility check)           │
                    │                                     │
                    │  • make run_icarus                   │
                    │    (pure-SV DPI smoke test)          │
                    │                                     │
                    │  Run nightly / before releases       │
                    └──────────┬──────────────────────────┘
                               │
                    ┌──────────v──────────────────────────┐
                    │  Tier 4: ModelSim/Questa            │
                    │  (gold standard DPI compliance)      │
                    │                                     │
                    │  • Manual run before major releases  │
                    │  • Required for advanced SV features │
                    └─────────────────────────────────────┘
```

### 8.6 Implementation Checklist

- [ ] **8.6.1** Install Icarus Verilog: `sudo apt install iverilog`
- [ ] **8.6.2** Create pure-SV Icarus testbench (`tests/tb_icarus.sv`) that uses DPI imports to call ISS functions directly
- [ ] **8.6.3** Verify Icarus can parse all existing SV files (catch syntax incompatibilities early)
- [ ] **8.6.4** Add Icarus `Makefile` target (`make run_icarus`)
- [ ] **8.6.5** Verify ISS→DPI→SV→MMIO round-trip works under Icarus
- [ ] **8.6.6** Document Icarus-specific limitations in `docs/integration.md`
- [ ] **8.6.7** (Optional) Evaluate ModelSim Intel FPGA Starter for pre-release DPI compliance checking
- [ ] **8.6.8** Add CI job for Tier 1 (standalone) and Tier 2 (Verilator); optionally Tier 3 (Icarus)

---

## Dependency & Build Order


```
All Phases 1-6 are now complete.
Remaining work: Phases 7 & 8
     ↓
Phase 7 (Performance tuning — can iterate alongside any phase)
Phase 8 (Cross-simulator verification — independent of Phase 7)
```

### Quick Wins (First Week) — ✅ All Completed

1. ✅ **M-extension** — Integer multiply/divide in firmware
2. ✅ **FENCE handling** — Prevents unknown opcode warnings
3. ✅ **Updated `isa_support.md`** — Full matrix with checkboxes and gap analysis
4. ✅ **IRQ polling in `rv_step()`** — Enables interrupt-driven firmware patterns
5. ✅ **Add Makefile `run_fast`/`run_debug` targets** — Performance and debug variants
6. ✅ **CSR support** — mstatus, mtvec, mepc, mcause, mvendorid, marchid, mhartid
7. ✅ **`rv_init_from_buffer()`** — Embedded firmware loading without file I/O
8. ✅ **C-extension expander** — Full compressed decoder in `rv32_dpi.c` (320 lines)
9. ✅ **AHB BFM + DPI→AHB bridge** — `ahb_lite_bfm.sv` + `tb_top_ahb.sv` with blocking handshake
10. ✅ **RTL DUT with AHB-Lite** — `ahb_gpio.sv` + `rv32_dpi_ahb_tb.cpp` end-to-end
11. ✅ **IRQ example** — `firmware_irq.S` + `rv32_dpi_irq_tb.cpp`
12. ✅ **ISA validation runner** — `run_riscv_tests.sh` + `rv32_dpi_riscv_tests.cpp`

### Remaining Work

1. **Performance tuning** — Batch size optimization, DPI crossing minimization (write merging), waveform strategy. Phase 7 provides detailed implementation guidance above.
2. ~~**Doc refresh** — Update `arch.md` and `integration.md` with current MMIO address ranges.~~ ✅ **Done — MMIO range updated to `0x1000_0000` – `0x100F_FFFF` with GPIO register map**
3. ~~**Run and capture ISA validation results** — Execute `make run_riscv_tests` and update `isa_support.md` checkboxes with actual pass/fail.~~ ✅ **Done — 48/48 tests pass (40 RV32I + 7 RV32M + 1 RV32C)**



---

## Files Summary

### Already Created

| File | Purpose | Phase |
|------|---------|-------|
| `dpi-riscv/firmware_irq.S` | IRQ-driven GPIO firmware | ✅ 4 |
| `dpi-riscv/sim/bus/ahb_lite_bfm.sv` | AHB-Lite BFM | ✅ 6 |
| `dpi-riscv/sim/harness/tb_top_ahb.sv` | AHB-wrapped testbench top | ✅ 6 |
| `dpi-riscv/sim/harness/rv32_dpi_irq_tb.cpp` | IRQ testbench | ✅ 4 |
| `dpi-riscv/tests/run_riscv_tests.sh` | ISA validation runner | ✅ 5 |

### Modified (changes applied)

| File | Changes | Phase |
|------|---------|-------|
| `dpi-riscv/sim/iss/rv32_dpi.c` | M-extension, C-ext dispatch, CSR, FENCE, IRQ polling | ✅ 1 |
| `dpi-riscv/sim/iss/rv32_dpi.h` | No changes needed (API already complete) | — |
| `dpi-riscv/docs/isa_support.md` | Full matrix with checkboxes and gap analysis | ✅ 5 |
| `dpi-riscv/Makefile` | Added testbench targets, `run_fast`, `run_debug`, `run_riscv_tests` | ✅ 4,7 |
| `dpi-riscv/sim/harness/README.md` | Documented new examples | ✅ 4 |

### Outstanding Doc Updates

| File | Changes Needed |
|------|----------------|
| ~~`dpi-riscv/docs/arch.md`~~ | ~~Update MMIO address range and architecture description to reflect current implementation~~ ✅ **Done** |
| ~~`dpi-riscv/docs/integration.md`~~ | ~~Update MMIO address range and integration steps~~ ✅ **Done** |
| ~~`dpi-riscv/docs/isa_support.md`~~ | ~~Run `make run_riscv_tests` and update checkboxes with actual pass/fail results~~ ✅ **Done** |



---

*End of execution plan. Each phase section above contains enough detail to begin implementation. Toggle to **Act Mode** to start coding.*
