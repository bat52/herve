# ISA Support Matrix

Target: RV32I (Base) + M (Multiply/Divide) + C (Compressed)

## RV32I Base Integer Instruction Set

- [x] LUI, AUIPC
- [x] JAL, JALR
- [x] BEQ, BNE, BLT, BGE, BLTU, BGEU
- [x] LB, LH, LW, LBU, LHU
- [x] SB, SH, SW
- [x] ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI
- [x] ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND
- [x] FENCE, ECALL, EBREAK (FENCE = NOP in single-threaded ISS)

## RV32M Standard Extension

- [x] MUL, MULH, MULHSU, MULHU
- [x] DIV, DIVU, REM, REMU

## RV32C Standard Extension

- [ ] TBD — Not yet implemented

## Zicsr / Privileged

- [x] CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI
- [x] MRET, ECALL, EBREAK
- [ ] WFI (no-op, handled by polling loop)
- [ ] SFENCE.VMA (no-op, no VM)

## Supported CSR Registers

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| 0x300   | mstatus | R/W | Machine status (MIE bit) |
| 0x305   | mtvec   | R/W | Trap vector base address |
| 0x341   | mepc    | R/W | Exception PC |
| 0x342   | mcause  | R/W | Exception cause |
| 0xF11   | mvendorid | R/O | Vendor ID (0) |
| 0xF12   | marchid   | R/O | Architecture ID (1) |
| 0xF14   | mhartid   | R/O | Hart ID (0) |

## Interrupt Handling

- [x] IRQ injection via `rv_set_irq()`
- [x] Vectored interrupt dispatch (mtvec + cause * 4)
- [x] MRET return from interrupt

## Known Gaps

| Feature | Status | Impact | Mitigation |
|---------|--------|--------|------------|
| C-extension (compressed) | ❌ Not implemented | Firmware compiled with `-march=rv32im` (no C) works; `-march=rv32imc` will fault | Compile firmware with `-march=rv32im` |
| Floating-point (RV32F/D) | ❌ Not implemented | FP instructions will fault | Compile with `-march=rv32im` (no float) |
| MMU / virtual memory | ❌ Not implemented | No paging support | Bare-metal only |
| Physical Memory Protection (PMP) | ❌ Not implemented | No memory protection | Bare-metal only |
| User-mode (U-mode) | ❌ Not implemented | All code runs in M-mode | Bare-metal only |
| Debug/trace support | ❌ Not implemented | No trigger module | Use VCD waveform dump |
| Exception fidelity | ⚠️ Partial | ECALL/EBREAK halt; no precise exception model | Sufficient for firmware validation |

## Reference Testbench

- **Primary:** [riscv-tests](https://github.com/riscv-software-src/riscv-tests) (rv32ui-p-*, rv32um-p-*)
- **Validation method:** Run each test binary through ISS, check `gp` register (x3) for pass/fail
- **Status:** Not yet run — requires test binary compilation and test runner script
