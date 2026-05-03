# Debugging Report: `rv32-p-lui` and `rv32-p-srai` Test Failures

## Root Cause Found and Fixed

### Symptom

Both `rv32ui-p-lui` and `rv32ui-p-srai` were failing with `gp=7` (test case #3 failed).

### Root Cause

**Bug in SRAI shift amount extraction in `rv32_dpi.c`.**

The original code in `execute_instruction()`, OP-IMM case (opcode `0x13`), funct3 `0x5`:

```c
// Original (buggy):
} else if ((insn >> 25) == 0x20) {
    write_reg(rd, (uint32_t)((int32_t)src1 >> (insn >> 20)));  // SRAI
}
```

The shift amount was extracted as `(insn >> 20)` **without masking to 5 bits** (`& 0x1f`).

For SRAI, the RISC-V encoding is:
- `funct7` = bits [31:25] = `0100000` (0x20)
- `shamt` = bits [24:20] (5 bits)

So `(insn >> 20)` = `(funct7 << 5) | shamt` = `(0x20 << 5) | shamt` = `0x400 | shamt`.

For `shamt = 1`, this gives `0x401` instead of `1`, causing a shift by 1025 bits instead of 1 bit. Since RV32 only uses the lower 5 bits of the shift amount, `0x401 & 0x1f = 1`, but without the mask, the C compiler's shift behavior for amounts >= 32 is undefined — on x86, `(int32_t)0xfffff000 >> 0x401` would shift by `0x401 & 0x1f = 1` (since x86 uses only the lower 5 bits), but on other architectures it could produce different results.

### Fix

Added `& 0x1f` mask to the shift amount extraction:

```c
// Fixed:
} else if ((insn >> 25) == 0x20) {
    uint32_t shamt = (insn >> 20) & 0x1f;
    uint32_t result = (uint32_t)((int32_t)src1 >> shamt);
    write_reg(rd, result);
}
```

### Why SLLI and SRLI weren't affected

- **SLLI** has `funct7 = 0x00`, so `(insn >> 20) = shamt` (correct without mask)
- **SRLI** has `funct7 = 0x00`, so `(insn >> 20) = shamt` (correct without mask)
- **SRAI** has `funct7 = 0x20`, so `(insn >> 20) = 0x400 | shamt` (WRONG without mask)

### Verification

After the fix, all 51 riscv-tests pass:

```
51/51 PASS
```

Including:
- `[PASS] rv32ui-p-lui (99 insn)`
- `[PASS] rv32ui-p-srai (290 insn)`
- `[PASS] rv32ui-p-sra (546 insn)`
- `[PASS] rv32ui-p-srl (540 insn)`
- `[PASS] rv32ui-p-srli (284 insn)`
- `[PASS] rv32ui-p-slli (275 insn)`
- `[PASS] rv32ui-p-sll (527 insn)`

### Files Modified

| File | Change |
|------|--------|
| `dpi-riscv/sim/iss/rv32_dpi.c` | Added `& 0x1f` mask to SRAI shift amount extraction (line 653) |
| `dpi-riscv/tests/debug_rv32_lui.md` | This report |

### Debugging Method

1. Added instruction-level tracing (enabled via `TRACE_INSNS` env var) to the ISS
2. Traced `rv32ui-p-lui` execution — observed SRAI instruction at PC `0x800001a4`
3. Identified that the shift amount was not masked to 5 bits
4. Applied the fix and verified all tests pass
