/*
 * tb_modelsim_ahb.sv — AHB GPIO IRQ self-test for ModelSim.
 *
 * Pure-SystemVerilog DPI-C testbench that loads firmware_ahb.bin into the
 * Herve ISS and tests the full IRQ injection path using a register-shadow
 * approach that mimics GPIO peripheral behavior.
 *
 * The AHB GPIO C++ testbench drives the AHB BFM through a request/ack
 * handshake that ticks the clock inline inside dpi_mmio_read/dpi_mmio_write.
 * This multi-cycle timing pattern is not possible in pure DPI-C functions
 * (which must be non-blocking). Instead, this testbench uses a register-shadow
 * approach:
 *
 *   - mmio_regs[0] = GPIO_OUT  (at 0x1000_0000)
 *   - mmio_regs[1] = GPIO_IE   (at 0x1000_0004)
 *   - mmio_regs[2] = GPIO_STATUS (at 0x1000_0008, bit 0 = ext_irq)
 *   - mmio_regs[0x40] = pass indicator (at 0x1000_0100, word index 64)
 *
 *   When GPIO_OUT[0] is written 1, auto-assert ext_irq and call
 *   rv_mti_set_irq(1). When GPIO_OUT[0] is written 0, de-assert ext_irq
 *   and call rv_mti_set_irq(0).
 *
 * Firmware flow (firmware_ahb.S):
 *   1. Configure mtvec to point to interrupt handler (at 0x100)
 *   2. Enable MIE in mstatus
 *   3. Enable GPIO interrupt (GPIO_IE = 1)
 *   4. Write GPIO_OUT = 1 (triggers ext_irq via register-shadow logic)
 *   5. Poll GPIO_OUT in a loop — when handler clears it, fall through
 *   6. Write 1 to pass indicator at 0x1000_0100, ebreak
 *
 * Build & run:
 *   cd dpi-riscv
 *   gcc -shared -fPIC -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
 *       sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
 *   cd sim/modelsim
 *   vlib work
 *   vlog tb_modelsim_ahb.sv
 *   vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_ahb
 *
 * Expected output:
 *   [PASS] GPIO_OUT cleared = 0
 *   [PASS] Pass indicator = 1
 *   RESULT: PASS
 */

module tb_modelsim_ahb;

    // ====================================================================
    // DPI-C imports: ISS control functions
    // ====================================================================
    import "DPI-C" function void rv_init(string firmware, int ram_size);
    import "DPI-C" function void rv_reset(int pc);
    import "DPI-C" context function int  rv_step(int max_insn);
    import "DPI-C" function int  rv_get_pc();
    import "DPI-C" function void rv_mti_set_irq(int mask);

    // ====================================================================
    // DPI-C exports: MMIO bridge callable from ISS C code
    // ====================================================================
    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    // MMIO register file (matches address range 0x1000_0000 - 0x1000_00FC)
    // Extended to 0x1000_0100 (word index 64) for the pass indicator
    reg [31:0] mmio_regs [0:127];

    // GPIO register shadow
    reg        ext_irq;

    // DPI export implementations with GPIO register-shadow logic
    function int dpi_mmio_read(int addr);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0200) begin
            dpi_mmio_read = mmio_regs[(addr - 32'h1000_0000) / 4];
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        integer idx;
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0200) begin
            idx = (addr - 32'h1000_0000) / 4;
            mmio_regs[idx] = data;

            // GPIO register-shadow logic:
            // When GPIO_OUT (offset 0x00, idx=0) bit 0 is written 1,
            // assert ext_irq and call rv_mti_set_irq(1).
            // When GPIO_OUT bit 0 is written 0, de-assert ext_irq
            // and call rv_mti_set_irq(0).
            if (idx == 0) begin
                if (data[0] && !ext_irq) begin
                    ext_irq = 1;
                    rv_mti_set_irq(1);
                    $display("  GPIO_OUT=1 -> ext_irq asserted");
                end else if (!data[0] && ext_irq) begin
                    ext_irq = 0;
                    rv_mti_set_irq(0);
                    $display("  GPIO_OUT=0 -> ext_irq de-asserted");
                end
            end

            // Update GPIO_STATUS (offset 0x08, idx=2) to reflect ext_irq
            if (idx == 2 || idx == 0) begin
                mmio_regs[2] = {31'b0, ext_irq};
            end
        end
    endfunction

    // ====================================================================
    // Test sequence
    // ====================================================================
    initial begin
        integer executed;
        integer pass;
        integer pc;

        $display("========================================");
        $display("  ModelSim DPI-C AHB GPIO Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        rv_init("firmware_ahb.bin", 1 << 20);
        rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Initialize registers to known values (prevents X propagation)
        mmio_regs[0] = 32'd0;  // GPIO_OUT
        mmio_regs[1] = 32'd0;  // GPIO_IE
        mmio_regs[2] = 32'd0;  // GPIO_STATUS
        ext_irq = 0;

        // ================================================================
        // Phase 1: Boot firmware
        //   firmware_ahb.S configures mtvec=0x100, enables MIE,
        //   sets GPIO_IE=1, writes GPIO_OUT=1, then polls GPIO_OUT.
        //
        //   When GPIO_OUT=1 is written, dpi_mmio_write triggers
        //   ext_irq=1 and calls rv_mti_set_irq(1).
        // ================================================================
        $display("");
        $display("--- Phase 1: Boot firmware ---");
        executed = rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after Phase 1: 0x%08x", pc);
        $display("GPIO_OUT = 0x%08x, ext_irq = %0d", mmio_regs[0], ext_irq);

        // ================================================================
        // Phase 2: IRQ fires, handler runs
        //   The ISS should have woken from WFI (or the poll loop),
        //   vectored to handler at 0x100, read GPIO_STATUS,
        //   written GPIO_OUT=0 (which de-asserts ext_irq), and mret.
        // ================================================================
        $display("");
        $display("--- Phase 2: IRQ handler ---");
        executed = rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after Phase 2: 0x%08x", pc);
        $display("GPIO_OUT = 0x%08x, ext_irq = %0d", mmio_regs[0], ext_irq);

        // ================================================================
        // Phase 3: Post-handler, write pass indicator
        //   After handler clears GPIO_OUT, the poll loop exits,
        //   firmware writes pass=1 to 0x1000_0100 and ebreak.
        // ================================================================
        $display("");
        $display("--- Phase 3: Post-handler ---");
        executed = rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after Phase 3: 0x%08x", pc);
        $display("GPIO_OUT = 0x%08x, pass = 0x%08x", mmio_regs[0], mmio_regs[64]);

        // ================================================================
        // Verify results
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        // GPIO_OUT should be 0 (cleared by handler)
        if (mmio_regs[0] == 32'd0) begin
            $display("[PASS] GPIO_OUT cleared = %0d", mmio_regs[0]);
        end else begin
            $display("[FAIL] GPIO_OUT = %0d (expected 0)", mmio_regs[0]);
            pass = 0;
        end

        // Pass indicator at 0x1000_0100 should be 1
        if (mmio_regs[64] == 32'd1) begin
            $display("[PASS] Pass indicator = %0d", mmio_regs[64]);
        end else begin
            $display("[FAIL] Pass indicator = 0x%08x (expected 1)", mmio_regs[64]);
            pass = 0;
        end

        $display("");
        $display("========================================");
        if (pass) begin
            $display("  RESULT: PASS");
        end else begin
            $display("  RESULT: FAIL");
        end
        $display("========================================");

        if (pass)
            $finish(0);
        else
            $finish(1);
    end

endmodule
