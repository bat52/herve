/*
 * tb_modelsim_irq.sv — IRQ + WFI test for ModelSim.
 *
 * Pure-SystemVerilog DPI-C testbench that loads firmware_irq.bin into the
 * Herve ISS, boots it, injects an external interrupt, and verifies that
 * the interrupt handler toggles GPIO_OUT.
 *
 * The test has two phases:
 *   Phase 1: Boot firmware (configures mtvec, MIE, GPIO_IE, enters WFI)
 *   Phase 2: Assert IRQ via rv_mti_set_irq(1), step ISS (wakes from WFI,
 *            vectors to handler, toggles GPIO_OUT, mret)
 *
 * Build & run:
 *   cd dpi-riscv
 *   gcc -shared -fPIC -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
 *       sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
 *   cd sim/modelsim
 *   vlib work
 *   vlog tb_modelsim_irq.sv
 *   vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_irq
 *
 * Expected output:
 *   [PASS] GPIO_OUT toggled = 1
 *   RESULT: PASS
 */

module tb_modelsim_irq;

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
    reg [31:0] mmio_regs [0:63];

    // GPIO register shadow
    reg [31:0] gpio_out;
    reg [31:0] gpio_ie;
    reg [31:0] gpio_status;
    reg        irq_pending;

    // DPI export implementations
    function int dpi_mmio_read(int addr);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0100) begin
            dpi_mmio_read = mmio_regs[(addr - 32'h1000_0000) / 4];
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0100) begin
            mmio_regs[(addr - 32'h1000_0000) / 4] = data;
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
        $display("  ModelSim DPI-C IRQ Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        rv_init("firmware_irq.bin", 1 << 20);
        rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Initialize GPIO_OUT to 0 (prevents X propagation)
        mmio_regs[0] = 32'd0;
        mmio_regs[1] = 32'd0;
        mmio_regs[2] = 32'd0;

        // ================================================================
        // Phase 1: Boot firmware
        //   firmware_irq.S configures mtvec=0x100, enables MIE,
        //   sets GPIO_IE=1, then enters WFI loop.
        // ================================================================
        $display("");
        $display("--- Phase 1: Boot firmware ---");
        executed = rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after Phase 1: 0x%08x", pc);

        // ================================================================
        // Phase 2: Assert IRQ and step
        //   Set irq_pending flag, call rv_mti_set_irq(1).
        //   The ISS should wake from WFI, vector to handler at 0x100,
        //   read GPIO_STATUS, toggle GPIO_OUT, and mret.
        // ================================================================
        $display("");
        $display("--- Phase 2: Assert IRQ ---");
        irq_pending = 1;
        rv_mti_set_irq(1);
        $display("IRQ asserted (rv_mti_set_irq(1))");

        executed = rv_step(100);
        $display("rv_step (vector): executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after vector: 0x%08x", pc);

        // ================================================================
        // Phase 3: Execute the interrupt handler
        //   rv_step returns 1 after vectoring (no instructions executed).
        //   Call rv_step again to execute the handler body:
        //     lw t1, 8(t0)   — read GPIO_STATUS
        //     lw t2, 0(t0)   — read GPIO_OUT
        //     xori t2, t2, 1 — toggle bit 0
        //     sw t2, 0(t0)   — write GPIO_OUT
        //     mret           — return from interrupt
        // ================================================================
        $display("");
        $display("--- Phase 3: Execute handler ---");
        executed = rv_step(100);
        $display("rv_step (handler): executed %0d instructions", executed);
        pc = rv_get_pc();
        $display("PC after handler: 0x%08x", pc);

        // ================================================================
        // Verify results
        //
        // The interrupt handler should have:
        //   - Read GPIO_STATUS (offset 0x08)
        //   - Read GPIO_OUT (offset 0x00), toggled bit 0, written back
        //   - mret
        //
        // GPIO_OUT (mmio_regs[0]) should be 1 (toggled from 0).
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        if (mmio_regs[0] == 32'd1) begin
            $display("[PASS] GPIO_OUT toggled = %0d", mmio_regs[0]);
        end else begin
            $display("[FAIL] GPIO_OUT = %0d (expected 1)", mmio_regs[0]);
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
