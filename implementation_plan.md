# Implementation Plan: Debug Failing Dhrystone Implementation for Herve ISS

## Overview

Debug and fix the Dhrystone benchmark runner (`rv32_dpi_dhrystone.cpp`) and the Herve ISS core (`rv32_dpi.c`) so that the Dhrystone ELF binary executes correctly on the Herve ISS, producing correct results comparable to Spike.

The Dhrystone benchmark (`dhrystone.riscv`) is built from the riscv-tests benchmark infrastructure. It links at `0x80000000` using `test.ld`, which places the `.tohost` section at `0x80001000`. The program uses HTIF (Host-Target Interface) for console I/O (`printf` → `putchar` → `syscall(SYS_write, ...)`) and program exit (`tohost_exit()`). The Herve ISS already has HTIF support in `rv32_dpi.c` (lines 159-215), but the Dhrystone runner (`rv32_dpi_dhrystone.cpp`) has several critical issues that prevent correct execution.

The existing riscv-tests ISA test runner (`rv32_dpi_riscv_tests.cpp`) works correctly for all 51 ISA tests because those tests are simple `.bin` files loaded at `0x80000000` that execute directly without needing startup code. The Dhrystone benchmark is different — it's a full ELF binary with a `_start` entry point that expects the `crt.S` startup sequence to run before `main()`.

## Root Cause Analysis

### Bug 1: `rv_reset()` clears HTIF state but `rv_step()` doesn't check HTIF writes during execution

The `rv_step()` function (line 1236) calls `execute_instruction()` which calls `write_u32()` which calls `handle_htif_write()`. The HTIF handler correctly detects program exit (bit 0 = 1) and sets `halted = true`. However, the `rv_step()` loop checks `halted` at the top of each iteration (line 1258), so the exit is detected on the *next* instruction fetch. This is actually correct behavior — the exit is detected one instruction after the `tohost` write.

**Verdict: Not a bug.** The HTIF exit mechanism works correctly.

### Bug 2: `rv_reset()` doesn't set `sp` (x2) to a valid stack address

The `crt.S` startup code (line 131-133) sets up the stack pointer:
```asm
add sp, a0, 1
sll sp, sp, STKSHIFT    ; STKSHIFT = 17 → sp = (cid+1) << 17 = 0x20000
add sp, sp, tp          ; tp = _end (aligned to 64 bytes)
```

The Dhrystone runner calls `rv_reset(entry)` where `entry` is the ELF entry point (`_start`). The `_start` function in `crt.S` will execute and set up the stack. However, `_start` also:
1. Clears all registers (lines 18-48)
2. Enables FPU/vector (lines 51-52)
3. Checks XLEN (lines 55-66)
4. Sets up trap vector (lines 110-111)
5. Sets global pointer (lines 114-117)
6. Sets thread pointer (lines 119-121)
7. Gets core ID and sets up stack (lines 124-136)
8. Jumps to `_init` (line 137)

**Verdict: The `_start` function should execute correctly** since the ELF is loaded at `0x80000000` and the entry point is `_start`. The startup code will set up the stack, TLS, and call `_init` → `main()`.

### Bug 3: `write_u32()` HTIF handling is broken for 64-bit tohost writes

The critical bug is in `write_u32()` (lines 217-237). The Dhrystone program's `syscall()` function writes a 64-bit pointer value to `tohost`:

```c
tohost = (uintptr_t)magic_mem;  // 64-bit write
```

The `tohost` variable is declared as `volatile uint64_t tohost` at the `.tohost` section (address `0x80001000`). The RISC-V `sw` instruction writes 32 bits at a time. The compiler generates two `sw` instructions: one for the lower 32 bits at `0x80001000`, and one for the upper 32 bits at `0x80001004`.

The `write_u32()` function at line 218-225 attempts to reconstruct the 64-bit value:
```c
uint64_t full = (addr == HTIF_BASE + 4)
    ? ((uint64_t)value << 32) | (htif_tohost & 0xFFFFFFFFULL)
    : ((htif_tohost & 0xFFFFFFFF00000000ULL) | value);
handle_htif_write(HTIF_BASE, full);
```

**BUG: The order of writes matters.** The `sw` to `HTIF_BASE + 4` (upper 32 bits) may arrive *before* the `sw` to `HTIF_BASE` (lower 32 bits). When the upper word is written first:
- `addr == HTIF_BASE + 4` → `full = (value << 32) | (htif_tohost & 0xFFFFFFFF)`
- But `htif_tohost` is still 0 (lower word not written yet)
- So `full = (upper << 32) | 0` — this is wrong, the lower 32 bits are missing

When the lower word is written first:
- `addr == HTIF_BASE` → `full = (htif_tohost & 0xFFFFFFFF00000000) | value`
- But `htif_tohost` is still 0 (upper word not written yet)
- So `full = 0 | lower` — this is also wrong, the upper 32 bits are missing

**The fix:** Buffer the two 32-bit writes and only call `handle_htif_write()` when both halves have been received.

### Bug 4: `write_u32()` HTIF handling doesn't handle the `tohost_exit()` case correctly

The `tohost_exit()` function writes `(code << 1) | 1` to `tohost`. This is a 64-bit value where bit 0 = 1 (exit flag). The same 64-bit reconstruction issue applies here.

### Bug 5: The Dhrystone runner doesn't parse Herve's console output

The `run_herve()` function (line 170) only checks the exit code. It doesn't capture the Dhrystone output (Microseconds per run, Dhrystones per second) because the HTIF console output goes to `stdout` via `putchar()`. The `DhrystoneResult` struct has fields for these values, but they're never populated for Herve.

**Fix:** Capture stdout during Herve execution and parse the Dhrystone output strings.

### Bug 6: The Dhrystone runner doesn't initialize the `.tohost` section to zero

The `rv_reset()` function clears `htif_tohost` and `htif_fromhost` in the ISS state, but the actual memory at `0x80001000` (the `.tohost` section) is not explicitly zeroed. The ELF loader zeroes BSS, but the `.tohost` section is placed before `.text` in the linker script and may not be in a PT_LOAD segment's BSS range.

**Fix:** Explicitly zero the HTIF region after loading the ELF.

### Bug 7: The `rv32_dpi_dhrystone.cpp` uses `rv_is_halted()` but the `rv_step()` loop may exit before the program is fully done

The `rv_step()` function returns 0 when it hits an ECALL/EBREAK instruction (line 1279-1280). But the Dhrystone program uses `tohost_exit()` which writes to `tohost` and then enters an infinite loop (`while (1);`). The `rv_step()` loop will:
1. Execute the `sw` to `tohost` → `halted = true`
2. On the next iteration, `rv_step()` checks `halted` at line 1258 and returns 0
3. The outer loop in `run_herve()` checks `rv_is_halted()` and exits

This should work correctly.

## [Types]

No new types are needed. The existing `DhrystoneResult` struct in `rv32_dpi_dhrystone.cpp` is sufficient.

## [Files]

### Files to modify:

1. **`dpi-riscv/sim/iss/rv32_dpi.c`** — Fix HTIF 64-bit write reconstruction in `write_u32()` and `handle_htif_write()`
2. **`dpi-riscv/sim/iss/rv32_dpi_dhrystone.cpp`** — Fix the Dhrystone runner to:
   - Zero the HTIF region after ELF loading
   - Capture and parse Herve's console output for Dhrystone metrics
   - Add proper error handling and diagnostics

### Files to create:

None.

### Files to delete/move:

None.

## [Functions]

### Modified functions in `rv32_dpi.c`:

1. **`write_u32()`** (line 217) — Fix 64-bit HTIF tohost reconstruction:
   - Add a static buffer to accumulate the two 32-bit halves of the 64-bit tohost value
   - Only call `handle_htif_write()` when both halves have been received
   - Handle the case where the upper word arrives before the lower word

2. **`handle_htif_write()`** (line 159) — No changes needed, the function itself is correct. The bug is in how `write_u32()` calls it.

3. **`rv_reset()`** (line 1214) — Add HTIF state reset for the new buffering mechanism.

### Modified functions in `rv32_dpi_dhrystone.cpp`:

1. **`run_herve()`** (line 170) — Add:
   - Zero the HTIF region (`0x80001000` to `0x80001020`) after ELF loading
   - Capture stdout output during execution (redirect to a string buffer)
   - Parse the captured output for "Microseconds for one run" and "Dhrystones per Second"
   - Populate `result.dhrystones_per_second` and `result.microseconds_per_run`

2. **`main()`** (line 309) — Add better error reporting and diagnostics.

## [Classes]

No classes are modified. The codebase uses C-style structs and functions.

## [Dependencies]

No new dependencies. The existing `rv32_dpi.c` and `rv32_dpi_dhrystone.cpp` compile with standard C/C++ libraries.

## [Testing]

### Test plan:

1. **Unit test: HTIF 64-bit write reconstruction** — Verify that two 32-bit `sw` writes to `HTIF_BASE` and `HTIF_BASE + 4` correctly reconstruct the 64-bit value regardless of write order.

2. **Integration test: Dhrystone on Herve** — Run `make run_dhrystone` and verify:
   - The program exits with code 0
   - Console output contains "Microseconds for one run" and "Dhrystones per Second"
   - The Dhrystones per Second value is reasonable (within an order of magnitude of Spike's result)

3. **Regression test: riscv-tests** — Run `make run_riscv_tests` to verify that the HTIF fix doesn't break the existing 51 ISA tests.

### Test files:

- Existing: `dpi-riscv/tests/test.sh` — may need updating to include dhrystone test
- Existing: `dpi-riscv/Makefile` — `run_dhrystone` target already exists

## [Implementation Order]

1. **Fix HTIF 64-bit write reconstruction in `rv32_dpi.c`** — This is the core bug. Add a static buffer to accumulate the two 32-bit halves of the 64-bit tohost value in `write_u32()`. Only call `handle_htif_write()` when both halves have been received.

2. **Fix `rv_reset()` to clear HTIF buffer state** — Add initialization of the new HTIF buffer fields.

3. **Fix `rv32_dpi_dhrystone.cpp` to zero HTIF region** — After loading the ELF, explicitly zero the memory at `0x80001000` to `0x80001020`.

4. **Fix `rv32_dpi_dhrystone.cpp` to capture and parse Herve output** — Redirect stdout during Herve execution to a string buffer, then parse for Dhrystone metrics.

5. **Build and test** — Run `make dhrystone.riscv && make rv32_dpi_dhrystone && make run_dhrystone` to verify the fix.

6. **Regression test** — Run `make run_riscv_tests` to verify no regressions in the existing ISA test suite.
