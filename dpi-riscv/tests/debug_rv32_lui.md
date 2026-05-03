# Debugging Plan: `rv32-p-lui` and `rv32-p-srai` Test Failures

## Overview

Two riscv-tests fail in the Herve ISS:
- **`rv32ui-p-lui`** — fails with `gp=7` (test case #3 failed)
- **`rv32ui-p-srai`** — fails with `gp=7` (test case #3 failed)

Both report `gp=7`. From the riscv-tests failure encoding in `riscv-tests/env/p/riscv_test.h`:
- `TESTNUM` = `gp` (register x3)
- `RVTEST_FAIL` encodes: `gp = (gp << 1) | 1`
- `gp=7` means test case #3 failed: `(3 << 1) | 1 = 7`

## Root Cause Hypothesis

**Both failures share a common root cause: the SRAI (shift right arithmetic immediate) instruction is buggy in the ISS.**

Evidence:
1. The `lui` test's test case #3 uses `lui` followed by `srai` to verify sign extension:
   ```asm
   lui ra, 0xfffff       # ra = 0xfffff000
   srai ra, ra, 1        # ra = 0xfffff800 (expected)
   li t2, -2048          # t2 = 0xfffff800
   bne ra, t2, fail      # should NOT branch
   ```
2. The `srai` test also fails at test case #3, which tests:
   ```asm
   lui a3, 0x80000       # a3 = 0x80000000
   srai a4, a3, 1        # expected a4 = 0xc0000000
   ```
3. All other LUI test cases (tests #2, #4, #5, #6) either don't use SRAI or pass successfully.

## Suspect Code in `rv32_dpi.c`

The SRAI implementation is in `execute_instruction()`, opcode `0x13` (OP-IMM), funct3 `0x5`:

```c
case 0x5:
    if ((insn >> 25) == 0x00) {
        write_reg(rd, src1 >> (insn >> 20));        // SRLI
    } else if ((insn >> 25) == 0x20) {
        write_reg(rd, (uint32_t)((int32_t)src1 >> (insn >> 20)));  // SRAI
    }
```

Possible bugs to investigate:
1. **funct7 mismatch** — SRAI requires `funct7 = 0x20` (bits [31:25] = 0100000). Verify the test binary encoding is correct and the ISS decodes it properly.
2. **Shift amount** — The shift amount is `(insn >> 20)`. For RV32, shamt is 5 bits (bits [24:20]), so `(insn >> 20)` returns a value 0-31. This is correct for SRAI.
3. **Sign extension correctness** — The cast to `(int32_t)` before `>>` is the standard way to get an arithmetic right shift in C. But on some compilers, right-shift of signed negative integers is implementation-defined (though in practice all modern compilers do arithmetic shift).

## Debugging Steps

### Step 1: Add Instruction-Level Tracing

Add a conditional `printf` in `execute_instruction()` and `execute_compressed()` to dump:
- PC
- Instruction bytes (hex)
- Decoded opcode, funct3, funct7
- Source register values
- Destination register and value written

Use a compile-time flag or environment variable to enable tracing only for specific tests.

### Step 2: Trace `rv32ui-p-lui` Execution

Compile and run the test with tracing enabled. Focus on the instructions at addresses:
- `0x800001a0` — `lui ra, 0xfffff` (insn = `0xfffff0b7`)
- `0x800001a4` — `srai ra, ra, 1` (insn = `0x4010d093`)
- `0x800001a8` — `li t2, -2048` (insn = `0x80000393`)
- `0x800001ac` — `bne ra, t2, fail` (should NOT branch)

Expected trace for the SRAI instruction:
```
PC=0x800001a4  insn=0x4010d093  OP-IMM  funct3=5  funct7=0x20  shamt=1
  src1 (x1)=0xfffff000  →  dst (x1)=0xfffff800
```

### Step 3: Trace `rv32ui-p-srai` Execution

Focus on test case #3 at address:
- `0x800001a4` — `lui a3, 0x80000` (insn = `0x800006b7`)
- `0x800001a8` — `srai a4, a3, 1` (insn = `0x4016d713`)

Expected:
```
PC=0x800001a8  insn=0x4016d713  OP-IMM  funct3=5  funct7=0x20  shamt=1
  src1 (x13)=0x80000000  →  dst (x14)=0xc0000000
```

### Step 4: Identify and Fix the Bug

Based on trace output, identify which of the following is wrong:

| Symptom | Likely Cause |
|---------|-------------|
| SRAI decoded as SRLI (logical shift) | funct7 check fails, falls through to `(insn >> 25) == 0x00` |
| Wrong shift amount | shamt extraction error |
| Wrong sign extension | `(int32_t)` cast issue on the C compiler |

Fix the identified issue in `execute_instruction()`.

### Step 5: Verify the Fix

1. Recompile `rv32_dpi_riscv_tests`
2. Run `rv32ui-p-lui` — should now PASS
3. Run `rv32ui-p-srai` — should now PASS
4. Run the full test suite — should show 51/51 PASS

## Deliverables

1. A small tracing patch to `rv32_dpi.c` (or a separate debug build)
2. Trace output showing the bug
3. A fix patch to `rv32_dpi.c`
4. Verification that all tests pass

## Files to Modify

| File | Change |
|------|--------|
| `dpi-riscv/sim/iss/rv32_dpi.c` | Add tracing code + fix the SRAI bug |
| `dpi-riscv/tests/debug_rv32_lui.md` | (this file) document the process and findings |

## References

- `rv32ui-p-lui.dump` — Disassembly of the LUI test binary
- `rv32ui-p-srai.dump` — Disassembly of the SRAI test binary
- `riscv-tests/isa/macros/scalar/test_macros.h` — TEST_CASE macro definition
- `riscv-tests/env/p/riscv_test.h` — RVTEST_PASS/RVTEST_FAIL and TESTNUM=gp encoding
- RISC-V spec: Chapter 2.4 (RV32I Immediate Encoding Variants), SRAI encoding
