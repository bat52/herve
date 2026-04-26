# RISC-V DPI Co-Simulation — Detailed Execution Plan

> **Generated:** 2026-04-26
> **Purpose:** Step-by-step implementation guide for building features described in `dpi.md`
> **Status:** Core ISS complete (RV32I + M + CSR + IRQ); examples working; gaps in C-extension, AHB BFM, DMA/IRQ examples
> **Last updated:** 2026-04-26 — Phase 1 (M-ext, FENCE, CSR, IRQ) and Phase 2 (dependency policy) are now implemented

---

## Table of Contents

1. [Phase 1 — Core ISS Extensions](#phase-1--core-iss-extensions)
2. [Phase 2 — No-Library Dependency Enforcement](#phase-2--no-library-dependency-enforcement)
3. [Phase 3 — Verilator DPI Integration](#phase-3--verilator-dpi-integration)
4. [Phase 4 — Example Implementation](#phase-4--example-implementation)
5. [Phase 5 — ISA Support Comparison](#phase-5--isa-support-comparison)
6. [Phase 6 — RTL Coupling, IRQ, DMA](#phase-6--rtl-coupling-irq-dma)
7. [Phase 7 — Performance Tuning](#phase-7--performance-tuning)
8. [Dependency & Build Order](#dependency--build-order)

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

**Still missing:**

| Feature | Code Location | Work Required |
|---------|--------------|---------------|
| C-extension (compressed) | New decoder logic before `execute_instruction` | ~200 lines |

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
| Memory copy / DMA trigger | — | ❌ Missing |
| Interrupt-driven GPIO toggle | — | ❌ Missing |
| RTL DUT with AHB | — | ❌ Missing |

### 4.1 New: DMA Memory Copy Example

**Purpose:** Demonstrate firmware that uses MMIO to configure a hypothetical DMA controller to copy a block of data.

**MMIO register map for DMAC:**

| Address | Name | Width | Access | Description |
|---------|------|-------|--------|-------------|
| `0x1000_0000` | `DMAC_SRC` | 32 | R/W | Source address |
| `0x1000_0004` | `DMAC_DST` | 32 | R/W | Destination address |
| `0x1000_0008` | `DMAC_LEN` | 32 | R/W | Transfer length (bytes) |
| `0x1000_000C` | `DMAC_CTRL` | 32 | R/W | Control register (bit 0 = START) |
| `0x1000_0010` | `DMAC_STATUS` | 32 | R | Status (bit 0 = BUSY, bit 1 = DONE) |

**Firmware flow (`firmware_dma.S`):**

```asm
_start:
    // Source data at address 0x100 (already populated)
    lui t0, 0x10000          // MMIO base = 0x1000_0000
    lui t1, 0x00001          // src = 0x00000100
    sw t1, 0(t0)             // DMAC_SRC = 0x100
    lui t1, 0x00200          // dst = 0x00200000
    sw t1, 4(t0)             // DMAC_DST = 0x200000
    addi t1, x0, 256         // len = 256 bytes
    sw t1, 8(t0)             // DMAC_LEN = 256
    addi t1, x0, 1           // START = 1
    sw t1, 12(t0)            // DMAC_CTRL = 1

    // Poll for completion
poll_loop:
    lw t1, 16(t0)            // read DMAC_STATUS
    andi t1, t1, 2           // check DONE bit
    beq t1, x0, poll_loop    // loop if not done

    // Verify: read back dest and compare
    // ... (read at 0x00200000)
    ebreak
```

**SV BFM for DMAC (`tb_top_dmac.sv`):**

```systemverilog
module tb_top_dmac;
    // ... DPI exports, MMIO read/write ...
    // Behavioral DMAC model: on START, copy memory[SRC..SRC+LEN] to memory[DST..DST+LEN]
    // Then assert DONE in STATUS register
endmodule
```

**C++ harness (`rv32_dpi_dmac_tb.cpp`):**

```cpp
// 1. Create Vtb_top_dmac model
// 2. Initialize ISS with shared RAM
// 3. Pre-populate source data at address 0x100 in RAM
// 4. rv_step(1000) — firmware configures DMAC
// 5. After rv_step returns, check that destination memory at 0x200000 matches source
// 6. PASS/FAIL comparison
```

### 4.2 New: Interrupt-Driven GPIO Toggle Example

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

### 4.3 New: RTL DUT with AHB-Lite BFM

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

`isa_support.md` exists with checkboxes, but all boxes are unchecked despite the majority of RV32I being implemented.

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

## Phase 6 — RTL Coupling, IRQ, DMA

### Current State

| Feature | Status | Notes |
|---------|--------|-------|
| AHB BFM | ❌ Missing | Signal-level access only |
| MMIO connected to DUT | ⚠️ Partial | Direct register access, no bus protocol |
| IRQ from RTL to ISS | ❌ Not wired | `rv_set_irq` exists but never called |
| Shared RAM | ✅ Working | Pointer returned by `rv_get_ram()` |
| DMA flow validation | ❌ Missing | |

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

**Current:** `rv_set_irq(mask)` stores the mask but nothing reads it.

**Proposed implementation:**

1. **SV side:** Testbench calls `rv_set_irq` via DPI import.
2. **ISS side:** `rv_step()` checks `irq_mask` at start of each batch.
3. **IRQ synchronization points:**
   - At the start of each `rv_step()` call
   - After every MMIO write (in case the write caused the RTL to assert IRQ)
   - After MMIO read that returns status indicating IRQ pending

**Timing diagram:**

```
Time:   |---ISS batch---|---RTL ticks---|---ISS batch---|---RTL ticks---|
       rv_step(1000)   toggle_clk()   rv_step(1000)   toggle_clk()
                              |
                         rtl_assert_irq()
                              ↓
                         rv_set_irq(1)  → checked at next rv_step
```

### 6.4 DMA Flow Validation

**Scenario:**

1. Firmware initializes source data in shared RAM at `0x00001000` (within shared RAM).
2. Firmware writes DMAC registers via MMIO to configure a transfer.
3. RTL DMAC model performs the copy (in SV, during clock ticks).
4. Firmware polls DMAC_STATUS until DONE.
5. Firmware reads destination area and verifies.

**Validating the flow:**

```c
// Pre-populate source data
uint32_t *ram = (uint32_t *)rv_get_ram();
ram[0x1000 / 4] = 0xDEADBEEF;
ram[0x1004 / 4] = 0xCAFEBABE;

// Run firmware (configures DMAC, polls for completion)
rv_reset(0);
int step_count = rv_step(10000);

// Check destination
uint32_t result1 = ram[0x200000 / 4];  // outside shared RAM? Or inside?
// If DMA operates on shared RAM, dest must also be within RAM range.
```

**Important constraint:** The DMAC copy only works if both source and destination are within the shared RAM region. If the DMAC is purely behavioral in SV, it needs access to the same memory buffer via the DPI export mechanism or direct model access.

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

## Dependency & Build Order

```
Phase 1.1 (M-extension)
     ↓
Phase 1.3 (FENCE)
     ↓
Phase 1.4 (CSR)
     ↓
Phase 1.5 (IRQ polling)
     ↓  ┌──────────────────────┐
     ↓  │ Phase 1.2 (C-ext)    │ ← Can be developed in parallel
     ↓  └──────────────────────┘
     ↓
Phase 2 (Dependency check — already compliant, just document)
     ↓
Phase 4.1 (DMA example)
Phase 4.2 (IRQ example)
     ↓
Phase 6.1 (AHB BFM)
Phase 6.2 (DPI→AHB bridge)
Phase 6.3 (IRQ wiring)
Phase 6.4 (DMA validation)
     ↓
Phase 5 (ISA comparison — needs ISS complete first)
     ↓
Phase 4.3 (RTL DUT with AHB)
     ↓
Phase 7 (Performance tuning — can iterate alongside any phase)
```

### Quick Wins (First Week) — ✅ All Completed

1. ✅ **M-extension** — Integer multiply/divide in firmware
2. ✅ **FENCE handling** — Prevents unknown opcode warnings
3. ✅ **Updated `isa_support.md`** — Full matrix with checkboxes and gap analysis
4. ✅ **IRQ polling in `rv_step()`** — Enables interrupt-driven firmware patterns
5. ✅ **Add Makefile `run_fast`/`run_debug` targets** — Performance and debug variants
6. ✅ **CSR support** — mstatus, mtvec, mepc, mcause, mvendorid, marchid, mhartid
7. ✅ **`rv_init_from_buffer()`** — Embedded firmware loading without file I/O

### Medium Term (Next Steps)

8. **C-extension expander** (~3 days) — Enables RISC-V toolchain to produce smaller firmware
9. **DMA example** (~2 days) — Demonstrates DMAC MMIO pattern

### Long Term (Third Week+)

9. **AHB BFM** (~2 days) — Bus protocol abstraction for real RTL integration
10. **RTL DUT example with interconnect** (~3 days) — End-to-end demo
11. **Performance tuning** (~2 days, ongoing) — Batch size, DPI crossing minimization, caching
12. **ISA validation suite** (~3 days) — Script to run riscv-tests, report gaps

---

## Files to Create / Modify Summary

### New Files

| File | Purpose | Phase |
|------|---------|-------|
| `dpi-riscv/firmware_dma.S` | DMA firmware example | 4 |
| `dpi-riscv/firmware_irq.S` | IRQ-driven GPIO firmware | 4 |
| `dpi-riscv/sim/bus/ahb_lite_bfm.sv` | AHB-Lite BFM | 6 |
| `dpi-riscv/sim/harness/tb_top_ahb.sv` | AHB-wrapped testbench top | 6 |
| `dpi-riscv/sim/harness/tb_top_dmac.sv` | DMAC behavioral model | 6 |
| `dpi-riscv/sim/harness/rv32_dpi_dmac_tb.cpp` | DMA testbench | 6 |
| `dpi-riscv/sim/harness/rv32_dpi_irq_tb.cpp` | IRQ testbench | 6 |
| `dpi-riscv/tests/run_riscv_tests.sh` | ISA validation runner | 5 |

### Modified Files

| File | Changes | Phase |
|------|---------|-------|
| `dpi-riscv/sim/iss/rv32_dpi.c` | M-extension, C-ext dispatch, CSR, FENCE, IRQ polling | 1 |
| `dpi-riscv/sim/iss/rv32_dpi.h` | No changes needed (API already complete) | — |
| `dpi-riscv/docs/isa_support.md` | Check off implemented instructions, add M/C columns | 5 |
| `dpi-riscv/Makefile` | Add new testbench targets, `run_fast`, `run_debug` | 4,7 |
| `dpi-riscv/sim/harness/README.md` | Document new examples | 4 |

---

*End of execution plan. Each phase section above contains enough detail to begin implementation. Toggle to **Act Mode** to start coding.*
