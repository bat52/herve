/*
 * tb_modelsim_basic.sv — Basic MMIO smoke test for ModelSim.
 *
 * Pure-SystemVerilog DPI-C testbench that loads firmware.bin into the
 * Herve ISS, executes it, and verifies the MMIO register values.
 *
 * Build & run:
 *   cd dpi-riscv
 *   gcc -shared -fPIC -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
 *       sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
 *   cd sim/modelsim
 *   vlib work
 *   vlog tb_modelsim_basic.sv
 *   vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_basic
 *
 * Expected output:
 *   [PASS] mmio_regs[0] (newline) = 0x0000000A
 *   [PASS] mmio_regs[1] = 0xDEADBEEF
 *   [PASS] mmio_regs[2] = 0x12345678
 *   RESULT: PASS
 */

module tb_modelsim_basic;

    // ====================================================================
    // DPI-C imports: ISS control functions
    // ====================================================================
    import "DPI-C" function void rv_init(string firmware, int ram_size);
    import "DPI-C" function void rv_reset(int pc);
    import "DPI-C" function int  rv_step(int max_insn);
    import "DPI-C" function int  rv_get_pc();

    // ====================================================================
    // DPI-C exports: MMIO bridge callable from ISS C code
    // ====================================================================
    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    // MMIO register file (matches address range 0x1000_0000 - 0x1000_00FC)
    reg [31:0] mmio_regs [0:63];

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
    // Helper: print MMIO register state
    // ====================================================================
    task print_mmio_regs();
        integer i;
        $display("MMIO register state:");
        for (i = 0; i < 4; i = i + 1) begin
            $write("  mmio_regs[%0d] = 0x%08x", i, mmio_regs[i]);
            if ((i + 1) % 4 == 0)
                $write("\n");
            else
                $write("  ");
        end
        $write("\n");
    endtask

    // ====================================================================
    // Test sequence
    // ====================================================================
    initial begin
        integer executed;
        integer pass;

        $display("========================================");
        $display("  ModelSim DPI-C Basic Smoke Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        rv_init("firmware.bin", 1 << 20);
        rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Execute firmware
        $display("Executing firmware...");
        executed = rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        $display("PC after execution: 0x%08x", rv_get_pc());

        // Print MMIO register state
        $display("");
        print_mmio_regs();

        // ================================================================
        // Verify results
        //
        // firmware.S writes to MMIO at 0x1000_0000:
        //   [0] = offset 0x000: last write is '\n' (0x0A)
        //   [1] = offset 0x004: 0xDEAD_BEEF
        //   [2] = offset 0x008: 0x12345678
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        // Last character written to offset 0x00
        if (mmio_regs[0] == 32'h0000_000A) begin
            $display("[PASS] mmio_regs[0] (newline) = 0x%08x", mmio_regs[0]);
        end else begin
            $display("[FAIL] mmio_regs[0] = 0x%08x (expected 0x0000000A)", mmio_regs[0]);
            pass = 0;
        end

        // Value at offset 0x04
        if (mmio_regs[1] == 32'hDEAD_BEEF) begin
            $display("[PASS] mmio_regs[1] = 0x%08x", mmio_regs[1]);
        end else begin
            $display("[FAIL] mmio_regs[1] = 0x%08x (expected 0xDEADBEEF)", mmio_regs[1]);
            pass = 0;
        end

        // Value at offset 0x08
        if (mmio_regs[2] == 32'h1234_5678) begin
            $display("[PASS] mmio_regs[2] = 0x%08x", mmio_regs[2]);
        end else begin
            $display("[FAIL] mmio_regs[2] = 0x%08x (expected 0x12345678)", mmio_regs[2]);
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
