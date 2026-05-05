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

- [x] C.ADDI, C.LI, C.LUI, C.NOP
- [x] C.ADDI4SPN, C.ADDI16SP
- [x] C.SLLI, C.SRLI, C.SRAI
- [x] C.ANDI, C.AND, C.OR, C.XOR, C.SUB
- [x] C.MV, C.ADD
- [x] C.J, C.JAL, C.JR, C.JALR
- [x] C.BEQZ, C.BNEZ
- [x] C.LW, C.SW, C.LWSP, C.SWSP
- [x] C.EBREAK
- [ ] C.FLW, C.FSW (RV32FC — FP not supported)
- [ ] C.LD, C.SD, C.LDSP, C.SDSP (RV64C — not applicable)
- [ ] C.FLD, C.FSD, C.FLDSP, C.FSDSP (RV64/DFP — not supported)
- [ ] C.ADDIW, C.ADDW, C.SUBW (RV64 — not applicable)

## Zicsr / Privileged

- [x] CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI
- [x] MRET, ECALL, EBREAK
- [x] WFI (wait-for-interrupt, implemented with sleep/wake)
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
| C-extension (compressed) | ✅ Implemented | All RV32C instructions supported (except FP/RV64 variants) | Compile firmware with `-march=rv32imc` for smaller code size |
| Floating-point (RV32F/D) | ❌ Not implemented | FP instructions will fault | Compile with `-march=rv32im` (no float) |
| MMU / virtual memory | ❌ Not implemented | No paging support | Bare-metal only |
| Physical Memory Protection (PMP) | ❌ Not implemented | No memory protection | Bare-metal only |
| User-mode (U-mode) | ❌ Not implemented | All code runs in M-mode | Bare-metal only |
| Debug/trace support | ❌ Not implemented | No trigger module | Use VCD waveform dump |
| Exception fidelity | ⚠️ Partial | ECALL/EBREAK halt; no precise exception model | Sufficient for firmware validation |

## Verification Status

### RV32M Standalone Test

A standalone ISS test (`sim/iss/rv32_dpi_test.cpp`) verifies all 8 M-extension instructions with 16 test cases covering:
- Basic multiply: MUL, MULH, MULHSU, MULHU
- Basic divide: DIV, DIVU, REM, REMU
- Edge cases: division by zero, remainder by zero, INT32_MIN / -1 overflow
- Large operand multiplication: 0x10000001^2 (tests both low and high halves)

**Status:** ✅ All 16/16 tests pass (verified via `make run_standalone`)

### Verilator-based Test

A Verilator test harness (`sim/harness/rv32_dpi_muldiv_tb.cpp`) provides the same test coverage using the full RTL simulation stack.

**Status:** ⚠️ Known Verilator DPI convergence issue — the DPI export calls from C code during `rv_step()` trigger "Active region did not converge" errors. The standalone test is the primary verification method.

### Reference Testbench (riscv-tests)

- **Primary:** [riscv-tests](https://github.com/riscv-software-src/riscv-tests) (rv32ui-p-*, rv32um-p-*, rv32uc-p-*)
- **Validation method:** Run each test binary through ISS, check `gp` register (x3) for pass/fail
- **Tool:** `make run_riscv_tests` (compiles and runs `rv32_dpi_riscv_tests.cpp`)
- **Status:** ✅ All 48/48 tests pass

#### Results by Category

| Category | Tests | Passed | Failed |
|----------|-------|--------|--------|
| RV32UI (base) | 40 | 40 | 0 |
| RV32UM (multiply/divide) | 7 | 7 | 0 |
| RV32UC (compressed) | 1 | 1 | 0 |
| **Total** | **48** | **48** | **0** |

Tests verified: `add`, `addi`, `and`, `andi`, `auipc`, `beq`, `bge`, `bgeu`, `blt`, `bltu`, `bne`, `fence_i`, `jal`, `jalr`, `lb`, `lbu`, `lh`, `lhu`, `lui`, `lw`, `or`, `ori`, `sb`, `sh`, `sll`, `slli`, `slt`, `slti`, `sltiu`, `sltu`, `sra`, `srai`, `srl`, `srli`, `sub`, `sw`, `xor`, `xori`, `simple`, `div`, `divu`, `mul`, `mulh`, `mulhsu`, `mulhu`, `rem`, `remu`, `rvc`

### HTIF Benchmark Support

The ISS now supports **riscv-tests benchmark programs** (median, dhrystone, multiply, etc.) that use the **HTIF (Host-Target Interface)** protocol for completion signaling, rather than the `gp=1 + EBREAK` convention used by ISA tests.

- **HTIF tohost address:** `0x80001000` (defined by `test.ld` linker script)
- **Completion signal:** Write `(exit_code << 1) | 1` to `tohost` (`uint64_t` at `0x80001000`)
- **Detection:** The HTIF-aware runner (`rv32_dpi_benchmark_htif.cpp`) peeks at RAM after each batch of `rv_step(1000)` calls
- **No ISS changes needed:** HTIF detection is done externally by the benchmark runner, not in the core ISS (`rv32_dpi.c`)

**Status:** ✅ Median benchmark supported via `make run_benchmark_htif`

