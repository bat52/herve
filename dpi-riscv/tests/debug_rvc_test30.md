# Debug Plan: rv32uc-p-rvc Test Case 30

## Final Status

- **FIXED — 48/48 tests PASS (0 FAIL, 0 SKIP)**
- Two root causes were found and fixed:
  1. **`rv_step()` always added `pc += 2` after compressed instructions** — even after jumps/branches that already set `pc`
  2. **C.LWSP immediate encoding used wrong bit layout** — used simple CI-format `{insn[6:2], 2'b00}` instead of the correct scattered layout `{insn[6:5], insn[12], insn[4:2], 2'b00}`

---

## 1. What Test 30 Does

From `riscv-tests/isa/rv64uc/rvc.S` lines 70–76:

```
RVC_TEST_CASE (30, ra, 0,              # expects ra=0 after execution
      li ra, 0;                         # ra = 0
      c.j 1f;                           # offset = forward to first 1:
      c.j 2f;                           # forward to 2: (should NOT execute)
    1:c.j 1f;                           # infinite loop at 1: (should NOT execute)
    2:j fail;                           # jump to "fail" (should NOT execute if C.J works)
    1:)                                 # falls through — ra still 0
```

**Expected execution path:**
1. `li ra, 0` → ra=0
2. `c.j 1f` → jump forward, skip `c.j 2f`, land at `1:c.j 1f`
3. `c.j 1f` → jump forward, skip `2:j fail`, land at `1:)`
4. Falls through — ra still 0, test passes

---

## 2. Root Cause #1: `pc += 2` After Compressed Jumps

### The Bug

In `rv_step()`, after executing a compressed instruction, the code unconditionally did:

```c
if ((insn & 0x3u) != 0x3u) {
    uint16_t c_insn = (uint16_t)(insn & 0xFFFFu);
    if (!execute_compressed(c_insn)) break;
    pc += 2;  // BUG: Always adds 2, even after jumps!
}
```

For jump-type compressed instructions (C.J, C.JAL, C.JALR, C.JR, C.BEQZ, C.BNEZ), `execute_compressed()` already sets `pc` to the jump target. The `pc += 2` then corrupts `pc`, making execution continue at jump target + 2 bytes.

### How It Affected Test 30

Disassembly of the test 30 region (from `riscv-tests/isa/rv32uc-p-rvc`):

```
80002130: 01e00193    li gp,30
80002134: 4081        li ra,0
80002136: a011        j 8000213a    (C.J, insn=0xa011, offset=+4)
80002138: a011        j 8000213c    (C.J, should NOT execute)
8000213a: a011        j 8000213e    (C.J, insn=0xa011, offset=+4)
8000213c: a211        j 80002240    (j fail, should NOT execute)
8000213e: 0001        nop
```

With the bug:
1. `c.j` at `0x80002136` sets `pc = 0x8000213a` (correct)
2. Then `pc += 2` pushes `pc` to `0x8000213c` **(WRONG — lands on `j fail`)**
3. Test fails with gp=61

### The Fix

Track whether `pc` changed during compressed instruction execution:

```c
if ((insn & 0x3u) != 0x3u) {
    uint16_t c_insn = (uint16_t)(insn & 0xFFFFu);
    uint32_t pc_before = pc;
    if (!execute_compressed(c_insn)) break;
    if (pc == pc_before) {
        pc += 2;
    }
}
```

Non-jump instructions leave `pc` unchanged, so `pc += 2` applies normally. Jump/branch instructions modify `pc` during execution, so `pc += 2` is skipped.

---

## 3. Root Cause #2: C.LWSP Immediate Encoding Bug

After fixing root cause #1, test 30 passed (gp=3 = PASS) but the overall test failed at gp=81 (test case 40). Test 40 involves `C.LWSP` and `C.SWSP` stack operations.

### The Bug

The original C.LWSP decoder used a simple CI-format immediate extraction:

```c
// WRONG: offset = {insn[6:2], 2'b00}
uint32_t offset = ((insn >> 2) & 0x1fu) << 2;
```

This treats bits `[6:2]` as a contiguous 5-bit field shifted left by 2. But C.LWSP has a **non-standard** immediate layout where `insn[12]` is interleaved between bits [5] and [4]:

| Offset bits | Source bits in instruction |
|-------------|----------------------------|
| imm[7:6]    | insn[6:5]                  |
| imm[5]      | insn[12]                   |
| imm[4:2]    | insn[4:2]                  |
| imm[1:0]    | 0 (implicit)               |

### Example: Instruction 0x4532

For `insn=0x4532` — binary `0100 0101 0011 0010`:
- C.LWSP encoding: funct3=010, rd=01010(x10), offset bits scattered

Old decoder computed: `(0x4532 >> 2) & 0x1f = 0x0C → offset = 0x0C << 2 = 48`
Correct offset: `{01, 0, 001, 00}` = binary `01000100` = **68**

### The Fix

```c
// offset = {insn[6:5], insn[12], insn[4:2], 2'b00}
uint32_t offset = ((insn >> 5) & 0x3u) << 6 |   // imm[7:6] = insn[6:5]
                  ((insn >> 12) & 0x1u) << 5 |  // imm[5] = insn[12]
                  ((insn >> 2) & 0x7u) << 2;    // imm[4:2] = insn[4:2]
```

### C.SWSP Encoder Verification

The C.SWSP decoder was checked and verified correct:

```c
// offset[7:6] = insn[11:10], offset[5:4] = insn[6:5]
uint32_t offset = ((insn >> 10) & 0x3u) << 6 |
                  ((insn >> 5) & 0x3u) << 4;
```

This matches the RISC-V specification and the `make_c_swsp()` encoder in `rv32_dpi_c_test.cpp`.

---

## 4. Step-by-Step Debug (Archived)

### Step 2a: Disassemble the Binary

```bash
riscv64-unknown-elf-objdump -d riscv-tests/isa/rv32uc-p-rvc.elf \
  | sed -n '/TEST_CASE 30/,/TEST_CASE 31/p'
```

Confirmed the C.J instruction `0xa011` encodes offset=+4:
- bits [12:8] = 00001 → offset bits [11,4,9,8,10] = 0
- bits [7:2] = 000100 → offset bits [6,7,3:1,5] = {0,0,001,0} = offset[6]=0, offset[5]=0, offset[3:1]=001=1, offset[7]=0
- offset = {0,0,0,0,0,0,0,001,0} with bit[0]=0 → 0b00000100 = +4

### Step 2b-2e: Add Trace Mode

Trace mode was added to `rv32_dpi.c` via compiler flags to confirm:
- C.J offset is correct (+4 for `0xa011`)
- Stale-PC detection does not fire (instructions < 50000 limit)
- No register corruption from prior tests

### C.J Encoding Cross-Check

| Offset bit | C.J encoding | ISS decoder | Match? |
|------------|-------------|-------------|--------|
| 0          | 0 (implied) | 0 (implied) | ✓ |
| 1          | insn[3]     | `((insn >> 3) & 0x7u) << 1` | ✓ |
| 2          | insn[4]     | (same as above) | ✓ |
| 3          | insn[5]     | (same as above) | ✓ |
| 4          | insn[11]    | `((insn >> 11) & 0x1u) << 4` | ✓ |
| 5          | insn[2]     | `((insn >> 2) & 0x1u) << 5` | ✓ |
| 6          | insn[7]     | `((insn >> 7) & 0x1u) << 6` | ✓ |
| 7          | insn[6]     | `((insn >> 6) & 0x1u) << 7` | ✓ |
| 8          | insn[9]     | `((insn >> 9) & 0x3u) << 8` | ✓ |
| 9          | insn[10]    | (same as above) | ✓ |
| 10         | insn[8]     | `((insn >> 8) & 0x1u) << 10` | ✓ |
| 11         | insn[12]    | `((insn >> 12) & 0x1u) << 11` | ✓ |

C.J encoding was confirmed **CORRECT**. The root cause was the `pc += 2` bug, not the C.J decoder.

---

## 5. Test Results

### Final Validation

```
=== RISC-V Tests Validation Suite ===
Found 48 test binaries

  [PASS] rv32uc-p-rvc (253 insn)
  [PASS] rv32ui-p-add (499 insn)
  [PASS] rv32ui-p-addi (276 insn)
  ... (all 48 pass) ...

===========================
  Total:   48
  Passed:  48
  Failed:   0
  Skipped:  0
===========================
```

### Standalone C Tests

```
=== RV32C (Compressed) Extension Test ===
  [PASS] C.LI x6, 42              = 0x0000002a
  [PASS] C.ADDI x6, 10            = 0x00000034
  [PASS] C.NOP                      = 0x00000034
  [PASS] C.MV x7, x6               = 0x00000034
  [PASS] C.ADD x7, x6              = 0x00000068
  [PASS] C.LUI x8, 0x10000         = 0x10000000
  [PASS] C.LW x9, 0(x8)            = 0x0000002a
  [PASS] C.SW x9, 8(x8)            = 0x0000002a
  [PASS] C.SLLI x6, 2              = 0x000000d0
  [PASS] C.SRLI x9, 2              = 0x0000000a
  [PASS] C.ANDI x9, 0xF            = 0x0000000a
  [PASS] C.SUB x9, x9              = 0x00000000
  [PASS] C.XOR x9, x9              = 0x00000000
  [PASS] C.OR x9, x9               = 0x00000000
  [PASS] C.AND x9, x9              = 0x00000000
  [PASS] C.LWSP x10, 0(sp)         = 0x00000000
  [PASS] C.SWSP x6, 0(sp)          = 0x000000d0
  [PASS] C.ADDI4SPN x11, 16        = 0x00080010
  [PASS] C.ADDI16SP -32            = 0x0007ffe0
  [PASS] C.J (jump over NOP)        = 0x00000001
  [PASS] C.BEQZ taken               = 0x00000001
  [PASS] C.BNEZ taken               = 0x00000001
  [PASS] C.SRAI x6, 2              = 0x00000034

Passed: 23 / 23
```

---

## 6. Changes Made

### File: `dpi-riscv/sim/iss/rv32_dpi.c`

**Fix #1 — `rv_step()`** (lines ~908-912):
```c
// Before (BUG):
pc += 2;  // always advances after any compressed insn

// After (FIX):
if (pc == pc_before) {
    pc += 2;  // only advance if pc wasn't changed by a jump/branch
}
```

**Fix #2 — `execute_compressed()` C.LWSP case** (lines ~412-413):
```c
// Before (WRONG):
uint32_t offset = ((insn >> 2) & 0x1fu) << 2;

// After (CORRECT):
uint32_t offset = ((insn >> 5) & 0x3u) << 6 |   // imm[7:6] = insn[6:5]
                  ((insn >> 12) & 0x1u) << 5 |  // imm[5] = insn[12]
                  ((insn >> 2) & 0x7u) << 2;    // imm[4:2] = insn[4:2]
```
