# Debug Plan: rv32uc-p-rvc Test Case 30

## Status

- **47/48 tests pass** in `run_riscv_tests.sh`
- **Sole failure:** `rv32uc-p-rvc` with **gp=61**
- gp=61 decodes as: `gp = (TESTNUM << 1) | 1 → TESTNUM = 30`
- **CA-format fix applied already** (rd_c → rs1_c for C.SUB/C.XOR/C.OR/C.AND).
  Before this fix, gp was 31 (test case 15); now gp=61 (test case 30), so the C.SUB fix helped pass test 15.

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

**What gp=61 means:** The test reached `2:j fail`, which jumps to `write_tohost` → calls `RVTEST_FAIL` → sets gp=61 and ECALLs.

**There are two possible root causes:**
- (A) The first `c.j` jumps to the wrong address, landing on `c.j 2f` instead of `1:c.j 1f`
- (B) The second `c.j` (`1:c.j 1f`) jumps to the wrong address, landing on `2:j fail` instead of `1:)`

---

## 2. Step-by-Step Debug

### Step 2a: Disassemble the Binary

Extract the exact instruction encoding around test case 30.

```bash
riscv64-unknown-elf-objdump -d riscv-tests/isa/rv32uc-p-rvc.elf \
  | sed -n '/TEST_CASE 30/,/TEST_CASE 31/p'
```

Check which 16-bit values the assembler generated for the C.J instructions. The C.J encoding is:

```
bit 15 14 13 12 | 11 10  9  8  7  6  5  4 |  3  2  1  0
 funct3=5=101  | imm[11] 4 9 8 10 6 7 3 1 | 1  0  1  0
                | 12  11 10 9 8  7 6 5  4 |
```

Offset[11] = insn[12], offset[10] = insn[8], offset[9:8] = insn[10:9],
offset[7] = insn[6], offset[6] = insn[7], offset[5] = insn[2],
offset[4] = insn[11], offset[3:1] = insn[5:3], offset[0] = 0.

Verify by extracting the bits from the raw instruction and computing the expected offset.

### Step 2b: Add Trace Mode to the ISS

Add a debug function to `rv32_dpi.c` that enables instruction tracing:

```c
// Near the top of execute_compressed():
#ifdef TRACE
static void trace_insn(uint32_t pc, const char *name, uint32_t insn) {
    static int count = 0;
    printf("TRACE[%d] PC=0x%08X  insn=0x%04X  (%s)\n", count++, pc, insn, name);
}
#define TRACE_COMPRESSED(name, insn) trace_insn(pc, name, insn)
#define TRACE_32(name, insn)         trace_insn(pc, name, insn)
#else
#define TRACE_COMPRESSED(name, insn)
#define TRACE_32(name, insn)
#endif
```

Insert `TRACE_COMPRESSED("C.J", c_insn)` in the C.J case, and similar markers for each compressed instruction type.

### Step 2c: Build with Trace and Run Single Test

```bash
g++ -DTRACE -I./sim/iss -I. -o rv32_dpi_riscv_tests_trace \
    sim/iss/rv32_dpi_riscv_tests.cpp sim/iss/rv32_dpi.c

# Run only the rvc test
./rv32_dpi_riscv_tests_trace /path/to/riscv-tests/isa/bin/ | head -200
```

From the trace, look for:
- Where execution goes after the `li ra, 0` instruction
- Whether `c.j` jumps to the correct target
- Whether the stale-PC detection fires prematurely

### Step 2d: Cross-Check Bit Assembly

If the trace shows the C.J offset is wrong, compare the decoder in `rv32_dpi.c` (lines 350-362) against the RISC-V specification:

| Offset bit | C.J encoding                 | Current code                                            |
|------------|------------------------------|----------------------------------------------------------|
| 0          | 0 (implicit)                 | 0 (implicit)                                             |
| 1          | insn[3]                      | `((insn >> 3) & 0x7u) << 1` → bit [3:1] holds offset[3:1] |
| 2          | insn[4]                      | (same)                                                   |
| 3          | insn[5]                      | (same)                                                   |
| 4          | insn[11]                     | `((insn >> 11) & 0x1u) << 4`                             |
| 5          | insn[2]                      | `((insn >> 2) & 0x1u) << 5`                              |
| 6          | insn[7]                      | `((insn >> 7) & 0x1u) << 6`                              |
| 7          | insn[6]                      | `((insn >> 6) & 0x1u) << 7`                              |
| 8          | insn[9] + insn[10]           | `((insn >> 9) & 0x3u) << 8`                              |
| 9          | insn[10]                     | (same)                                                   |
| 10         | insn[8]                      | `((insn >> 8) & 0x1u) << 10`                             |
| 11         | insn[12] (sign)              | `((insn >> 12) & 0x1u) << 11`                            |

This looks correct, but verify with actual opcodes from Step 2a.

### Step 2e: Check for a Simpler Root Cause

If the C.J encoding is correct, the problem might be:
1. **Stale-PC detection** triggering too early (if the `c.j 1f` at `1:` creates a tight loop that gets flagged as "stale")
2. **A previous test case corrupting state** (e.g., `li sp, 0x1234` from test 3 affecting the stack)
3. **C.JAL from test case 37 leaving something on the stack that affects C.J**

Check the instruction count: the test runs 180 instructions (from the test output), which is well under the 50000-instruction limit, so it's not a timeout issue.

---

## 3. Resolution Path

| Root Cause Found | Fix |
|---|---|
| **C.J offset wrong** | Correct the bit encoding in `execute_compressed()` case 0x5 |
| **Previous test corrupts state** | Add `rv_reset()`-style register clearing between tests, or fix the specific corrupting instruction |
| **Stale-PC false positive** | Increase `MAX_STALE_CHECKS` or add PC-change tracking that handles intentional loops |
| **C.JAL overlaps with C.J decoding** | Verify that case 0x1 (funct3=1) for C.JAL doesn't interfere with case 0x5 (funct3=5) for C.J |

---

## 4. Post-Fix Validation

After fixing, run the full suite:

```bash
make clean && make rv32_dpi_riscv_tests && ./rv32_dpi_riscv_tests
```

Expected: 48/48 PASS, 0 FAIL, 0 SKIP.

Then update `isa_support.md` with the final results.
