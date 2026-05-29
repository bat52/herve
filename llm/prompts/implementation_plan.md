# Implementation Plan

Implement the RISC-V WFI (Wait For Interrupt) instruction in the ISS and update the IRQ testbench to leverage it for cleaner, more deterministic test orchestration.

The current ISS treats WFI as a no-op (the firmware uses a `j main_loop` spin loop instead). The testbench (`rv32_dpi_irq_tb.cpp`) relies on step-count-based timing to orchestrate IRQ injection and handler execution, which is fragile and wastes CPU cycles spinning. By implementing proper WFI semantics, the firmware can halt at a known point, and the testbench can use the natural stop behavior of `rv_step()` to precisely control each phase without counting instructions.

[Types]
No new types are required — only internal state additions and existing function modifications.

The only new data is a single `static bool wfi_sleep = false;` variable in `rv32_dpi.c` to track whether the ISS has executed WFI and is awaiting an interrupt.

[Files]
Four files will be modified, no new files will be created.

- **`sim/iss/rv32_dpi.c`** — Implement WFI instruction behavior:
  - Add `static bool wfi_sleep = false;` state variable (initialized to false in `rv_init`, `rv_init_from_buffer`, `rv_init_elf`, `rv_reset`)
  - In `execute_instruction()`, opcode 0x73 (SYSTEM), funct3==0 section: replace the WFI no-op (`break;`) with real logic:
    - If MIE is set (`csr_mstatus & 0x8`) and `irq_mask != 0`, treat WFI as a NOP (interrupt is pending, will be taken at next rv_step top)
    - Otherwise, set `wfi_sleep = true` and return `false` to break the instruction loop
  - In `rv_step()`, before the interrupt check:
    - If `wfi_sleep` is true: check if `irq_mask != 0 && (csr_mstatus & 0x8)`; if so, clear `wfi_sleep` and proceed to the interrupt handling code; otherwise return 0 immediately
  - Ensure `wfi_sleep` is reset to false in all initialisation paths (`rv_init`, `rv_init_from_buffer`, `rv_init_elf`, `rv_reset`, `rv_set_ram`)

- **`firmware_irq.S`** — Modify main loop to use WFI:
  - Replace `j main_loop` with `wfi` followed by `j main_loop` (standard RISC-V pattern: WFI may return spuriously, so the loop ensures we re-enter sleep)
  - The `.org 0x100` alignment for the interrupt handler must be preserved

- **`sim/harness/rv32_dpi_irq_tb.cpp`** — Simplify test phases to use WFI's natural stop behavior:
  - Phase 1: Boot firmware (single `rv_step(BATCH)` — returns when WFI is encountered)
  - Phase 2: Assert IRQ (`tb->irq = 1`), tick clock x2 (SV DPI sets `rv_set_irq(1)`)
  - Phase 3: Vector to handler (`rv_step(BATCH)` — returns 1 for the vectoring) and tick x2
  - Phase 4: Run handler (`rv_step(BATCH)` — executes handler, mret, back to WFI, returns count ≈6)
  - Phase 5: Check GPIO_OUT toggled
  - Phase 6: De-assert IRQ, tick x2
  - Phase 7: Inject second IRQ, vector, run handler, check GPIO_OUT toggled back
  - Verification: GPIO_OUT checks and PC range checks remain the same

- **`docs/isa_support.md`** — Update WFI status:
  - Change "WFI (no-op, handled by polling loop)" to "[x] WFI (wait-for-interrupt, implemented with sleep/wake)"

[Functions]
One new internal state variable, modifications to two existing functions and three initialization paths.

- **Modified: `rv32_dpi.c :: execute_instruction()`** (line ~516)
  - In the SYSTEM (opcode 0x73) -> funct3==0 section, replace the WFI no-op with an implementation that checks MIE and irq_mask, and either continues (interrupt pending) or sets wfi_sleep and returns false

- **Modified: `rv32_dpi.c :: rv_step()`** (line ~1130)
  - Add a WFI wake-up check at the top of the function, BEFORE the existing interrupt check:
    ```
    if (wfi_sleep) {
        if (irq_mask != 0 && (csr_mstatus & 0x8)) {
            wfi_sleep = false;  // wake up
        } else {
            return 0;  // still sleeping
        }
    }
    ```

- **Modified: `rv32_dpi.c :: rv_init()`** (line ~1048) — Add `wfi_sleep = false;` after initialization
- **Modified: `rv32_dpi.c :: rv_init_from_buffer()`** (line ~1082) — Add `wfi_sleep = false;`
- **Modified: `rv32_dpi.c :: rv_init_elf()`** (line ~904) — Add `wfi_sleep = false;` in the initialization section
- **Modified: `rv32_dpi.c :: rv_reset()`** (line ~1112) — Add `wfi_sleep = false;`
- **Modified: `rv32_dpi.c :: rv_set_ram()`** (line ~1185) — Add `wfi_sleep = false;`

[Classes]
No classes are modified. This is a strictly C-based codebase with no classes.

[Dependencies]
No new dependencies. The implementation uses only the already-permitted standard C headers.

[Testing]
The existing test.sh infrastructure will validate all changes.

- **`make run_irq_standalone`** — The standalone IRQ test (`rv32_dpi_irq_test.cpp`) builds firmware programmatically. Since it does not include a WFI instruction in its firmware, WFI behavior change does not affect it directly. However, it should still pass as a regression test.
- **`make run_irq`** — The Verilator-based test validates the new WFI implementation end-to-end: firmware boots, WFI sleeps, IRQ injected, handler runs, GPIO_OUT toggles, post-IRQ state verified.
- **`make run_standalone`**, **`make run_c_test`**, **`make run_riscv_tests`** — Regression tests verifying existing functionality is not broken.
- Manual verification: WFI should cause `rv_step()` to return immediately when no interrupt is pending, eliminating wasted spin-loop cycles.

[Implementation Order]
The changes must be implemented in dependency order to keep the system testable at each step.

1. **Implement WFI in `rv32_dpi.c`**: Add `wfi_sleep` state, modify `execute_instruction()`, `rv_step()`, and all init/reset paths. Compile standalone to verify it builds.
2. **Update `firmware_irq.S`**: Replace `j main_loop` with `wfi / j main_loop` pattern. Rebuild firmware.
3. **Update `sim/harness/rv32_dpi_irq_tb.cpp`**: Simplify test phases using WFI's natural stop. Rebuild and run `make run_irq`.
4. **Update `docs/isa_support.md`**: Mark WFI as implemented.
5. **Run full test suite**: `make clean && bash tests/test.sh` to verify all tests pass.
