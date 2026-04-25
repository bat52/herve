RISC-V DPI Co-Simulation Plan (Coding Agent Configuration)

Objective

Implement a lightweight RISC-V RV32 emulator integrated via DPI-C to accelerate RTL simulation of a hardware IP interacting with software.

Goals

- Support Verilator-based RTL/emulator co-simulation.
- Provide runnable examples for harness, firmware, and RTL integration.
- Enforce zero external library dependencies in the emulator runtime.
- Validate ISA support by comparing against a proven RISCV ISA testbench.

---

High-Level Architecture

RISC-V ISS (C, DPI)
    ↓ load/store
MMIO hooks (DPI-C)
    ↓
SystemVerilog bridge
    ↓
RTL DUT (AHB/AXI/etc.)

Key principle: run ISS freely, synchronize only on MMIO/IRQ events.

---

Components

1. Minimal RISC-V Emulator (C)

- Based on a trimmed RV32I/IMC core.
- Features:
  - RV32I base + selected M/C extensions.
  - Flat memory model with optional shared RAM pointer.
  - Step-based execution API.
  - MMIO interception and redirect to SystemVerilog callbacks.
  - IRQ injection support.
- No dependencies beyond standard C headers and Verilator DPI runtime.

Public API ("rv32_dpi.h")

- "rv_init(const char *firmware, int ram_size)"
- "rv_reset(uint32_t pc)"
- "rv_step(int max_instructions)"
- "rv_set_irq(uint32_t mask)"
- "rv_get_ram(void)"
- "rv_get_pc(void)"

---

2. Verilator / DPI Interface

Imported into SystemVerilog / Verilator harness:

import "DPI-C" function void rv_init(string fw, int ram_sz);
import "DPI-C" function int  rv_step(int max_insn);
import "DPI-C" function void rv_set_irq(int mask);
import "DPI-C" function longint rv_get_ram();
import "DPI-C" function int  rv_get_pc();

Exported to C:

export "DPI-C" function dpi_mmio_read;
export "DPI-C" function dpi_mmio_write;

---

3. MMIO Bridge

In C:

- Detect MMIO address ranges.
- Redirect MMIO to DPI callbacks.

if (is_mmio(addr)) {
    return dpi_mmio_read(addr);
}

In SystemVerilog:

function int dpi_mmio_read(int addr);
    return ahb_read(addr);
endfunction

function void dpi_mmio_write(int addr, int data);
    ahb_write(addr, data);
endfunction

---

4. RTL Integration for Verilator

- Use a Verilator C++ testbench or SystemVerilog harness.
- Wrap DUT with a bus functional model (BFM) exposing:
  - "ahb_read(addr)"
  - "ahb_write(addr, data)"
- Ensure proper ready/valid handshake and clocking.
- Avoid combinational loops between DPI and RTL.

---

5. Simulation Scheduling

Core loop in Verilator harness or SystemVerilog testbench:

while (!done) begin
    rv_step(1000); // run ahead
    for (int i = 0; i < clk_cycles; i++) begin
        toggle_clock();
    end
end

Guidelines:

- Batch size: 100–10k instructions.
- Synchronize only on:
  - MMIO access.
  - IRQ.
- Avoid one instruction per cycle unless debugging.

---

6. Interrupt Handling

- RTL → ISS:

rv_set_irq(1 << irq_id);

- ISS behavior:
  - Poll pending interrupts.
  - Jump to fixed exception vector or simplified trap handler.

---

7. Memory Model

Shared RAM strategy:

- Single memory buffer shared by ISS and RTL.
- ISS uses pointer access.
- RTL/Verilator testbench can map physical memory directly.

Advantages:

- Zero-copy DMA.
- No transaction overhead.
- Easier host-side firmware loading.

---

8. Examples and Validation

Example targets:

- Bare-metal firmware examples:
  - Hello world / MMIO write.
  - Memory copy / DMA trigger.
  - Interrupt-driven GPIO toggle.
- Verilator harness example:
  - Build Verilator model.
  - Instantiate DPI interface.
  - Run example firmware against simple RTL DUT.
- End-to-end example:
  - Firmware binary + shared RAM + MMIO bridge + RTL device.

Validation:

- Include a "smoke" example that executes simple instruction sequences.
- Include an MMIO read/write example.
- Include an interrupt example.

---

Implementation Tasks

Phase 1 — Core ISS

- [ ] Implement minimal RV32I decoder:
  - ADD, SUB
  - ADDI
  - LOAD/STORE (word)
  - JAL
- [ ] Add control-flow and immediates:
  - BEQ/BNE, BGE/BLT
  - LUI, AUIPC
- [ ] Add optional extensions gradually:
  - CSR support minimal.
  - M-extension multiply/divide.
  - C-extension compressed decode support.

Phase 2 — No-Library Dependency Enforcement

- [ ] Use only standard C headers (<stdint.h>, <stddef.h>, <stdbool.h>, <string.h>, <stdlib.h>).
- [ ] Avoid libc features not available in bare-metal or Verilator DPI.
- [ ] Implement firmware loader and string helpers manually if needed.
- [ ] Document dependency policy in README or design notes.

Phase 3 — Verilator DPI Integration

- [ ] Expose ISS API via DPI.
- [ ] Implement MMIO callbacks.
- [ ] Validate with a Verilator testbench.
- [ ] Ensure compatibility with Verilator build flow.

Phase 4 — Example Implementation

- [ ] Create example firmware binaries and source.
- [ ] Add Verilator harness example.
- [ ] Add RTL integration example with simple DUT.
- [ ] Add README usage instructions for examples.

Phase 5 — ISA Support Comparison

- [ ] Select a proven RISC-V ISA testbench reference.
- [ ] Create comparison checklist of supported instructions.
- [ ] Run selected testbench or canonical instruction suite.
- [ ] Document gaps between the emulator and the reference testbench.
- [ ] Use results to refine supported ISA subset.

Phase 6 — RTL Coupling, IRQ, DMA

- [ ] Implement AHB BFM.
- [ ] Connect MMIO to DUT.
- [ ] Validate read/write transactions.
- [ ] Add IRQ injection path.
- [ ] Share RAM buffer with RTL.
- [ ] Validate DMA flows.

Phase 7 — Performance Tuning

- [ ] Tune instruction batch size.
- [ ] Minimize DPI crossings.
- [ ] Disable unnecessary waveform dumping.

---

Constraints / Assumptions

- Bare-metal firmware only (no OS).
- No MMU / virtual memory.
- No cache modeling.
- Deterministic single-thread execution.
- No external runtime or simulation libraries beyond Verilator DPI.

---

ISA Comparison and Quality

- Track supported ISA subset against a known RISC-V ISA testbench.
- Use the testbench as a reference for instruction semantics and corner cases.
- Clearly mark unsupported instructions and extensions.
- Prioritize compatibility with common RV32IMC toolchains.

---

Known Limitations

- Incomplete ISA → compiler incompatibilities possible.
- No exception fidelity.
- Initial implementation may only support a minimal subset; expand with validation.

- No memory ordering guarantees
- Not suitable for Linux or complex drivers

---

Debug Strategy

- Log MMIO accesses
- Track PC around transactions
- Enable waveform dump only on failure
- Add instruction trace (optional)

---

Success Criteria

- Firmware can:
  - Configure DUT registers
  - Trigger operations
  - Handle interrupts
- Simulation speed significantly faster than RTL CPU
- Deterministic reproducibility

---

Optional Extensions

- Add M extension (MUL/DIV)
- Support byte/halfword accesses
- Implement basic CSR handling

---

Key Design Principle

«Keep the ISS functionally accurate but temporally decoupled.
Synchronize only when hardware interaction requires it.»

---

Codebase

Ignore LICENSE file from context.
Ignore files under folder /llm