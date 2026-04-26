---
name: dpi
description: "Use when implementing or reviewing a DPI-based RISC-V RV32 emulator co-simulation plan."
tags:
  - dpi
  - risc-v
  - verilator
  - co-simulation
---

This skill captures the implementation plan for a lightweight RISC-V RV32 emulator integrated via DPI-C.

It is designed to guide both implementation and review of a Verilator-based RTL co-simulation flow.

Key focus areas:

- Minimal RV32I/IMC emulator API exposed through Verilator DPI
- MMIO interception and DPI callback structure between C and SystemVerilog
- Shared RAM model for zero-copy host/RAM integration
- Simulation scheduling and rendezvous around MMIO/IRQ events
- Example harnesses, firmware, and validation paths

Implementation responsibilities:

1. Core emulator
   - Build a trimmed RV32I decoder with base instructions, control flow, and memory ops
   - Support a flat memory model and optional shared RAM pointer
   - Expose step-based execution, reset, IRQ injection, and state query APIs

2. DPI interface
   - Implement the public C API in `rv32_dpi.h`
   - Add DPI export callbacks for MMIO read/write operations
   - Keep data crossing between C and SystemVerilog minimal and deterministic

3. MMIO/RTL bridge
   - Detect MMIO address ranges inside the emulator and route them to SystemVerilog callbacks
   - Provide example RTL callback stubs such as `ahb_read`, `ahb_write`, or equivalent bus functions
   - Ensure the bridge avoids combinational loops and supports handshake semantics

4. Simulation schedule
   - Use batched `rv_step()` execution to advance the ISS in chunks
   - Synchronize only on MMIO or IRQ events to keep RTL and ISS decoupled
   - Tune batch size for performance while preserving correctness

5. IRQ handling
   - Expose `rv_set_irq()` to inject pending interrupts from RTL into the ISS
   - Implement simplified trap vector handling inside the emulator

6. Examples and validation
   - Provide firmware examples for MMIO writes, memory copy, and interrupt-driven I/O
   - Add a Verilator harness example that runs firmware against a sample DUT
   - Include smoke tests validating instruction execution and MMIO behavior

7. Documentation and validation
   - Document the no-dependency policy and supported ISA subset
   - Compare supported instructions against a reference RISCV ISA testbench
   - Capture gaps and refinement tasks in README/design notes

Use this skill when reviewing or implementing the DPI-based co-simulation architecture, verifying that the design remains lightweight, dependency-free, and focused on Verilator/RTL integration.
