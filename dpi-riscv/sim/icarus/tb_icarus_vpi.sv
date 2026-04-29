/*
 * tb_icarus_vpi.sv — Icarus Verilog VPI testbench for RV32 DPI ISS.
 *
 * This testbench uses VPI (Verilog Procedural Interface) system functions
 * instead of DPI-C import/export, because Icarus Verilog 12.0 does NOT
 * support the SystemVerilog DPI-C import/export syntax.
 *
 * Provided VPI system functions (from rv32_dpi_vpi.vpi):
 *   $rv_init(fw, ram_sz)      — Load firmware binary into ISS
 *   $rv_reset(pc)             — Reset ISS to given PC
 *   $rv_step(max_insn)        — Execute up to max_insn instructions (returns count)
 *   $rv_get_reg(reg)          — Read x[reg] value
 *   $rv_get_pc()              — Read current PC
 *   $vpi_read_mmio(idx)       — Read MMIO register at index idx
 *   $vpi_print_mmio()         — Print MMIO register state
 *
 * Build & run:
 *   cd dpi-riscv
 *   make firmware
 *   make run_icarus
 *
 * Or manually:
 *   gcc -shared -fPIC -I./sim/iss -I/usr/include/iverilog \
 *       -o obj_dir/rv32_dpi_vpi.vpi \
 *       sim/vpi/rv32_dpi_vpi.c sim/iss/rv32_dpi.c
 *   iverilog -g2012 -o obj_dir/tb_icarus.vvp sim/icarus/tb_icarus_vpi.sv
 *   vvp -Mobj_dir -mrv32_dpi_vpi obj_dir/tb_icarus.vvp
 *
 * This testbench verifies the ISS->VPI->MMIO round-trip:
 *   1. Load firmware.bin into the ISS
 *   2. Execute instructions until ebreak
 *   3. Verify MMIO registers contain expected values
 */

module tb_icarus_vpi;

    // ====================================================================
    // Test sequence
    // ====================================================================
    initial begin
        int executed;
        int pc;
        int pass;
        int mmio_val;

        // VCD waveform dump
        $dumpfile("tb_icarus_vpi.vcd");
        $dumpvars(0, tb_icarus_vpi);

        $display("========================================");

        $display("  Icarus Verilog VPI Smoke Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        $rv_init("firmware.bin", 1 << 20);
        $rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Execute firmware
        $display("Executing firmware...");
        executed = $rv_step(100);
        $display("rv_step: executed %0d instructions", executed);
        $display("PC after execution: 0x%08x", $rv_get_pc());

        // Print MMIO register state
        $display("");
        $vpi_print_mmio();

        // ================================================================
        // Verify results
        // ================================================================
        //
        // firmware.S writes to MMIO at 0x1000_0000:
        //   [0] = offset 0x000: 'H' (0x48), 'e' (0x65), 'l' (0x6C) ... last write wins
        //   [1] = offset 0x004: 0xDEAD_BEEF
        //   [2] = offset 0x008: 0x12345678
        //
        // Since firmware writes multiple characters to offset 0x00,
        // the last value stored in regs[0] should be '\n' (0x0A).
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        // Last character written to offset 0x00
        mmio_val = $vpi_read_mmio(0);
        if (mmio_val == 32'h0000_000A) begin
            $display("[PASS] mmio_regs[0] (newline) = 0x%08x", mmio_val);
        end else begin
            $display("[FAIL] mmio_regs[0] = 0x%08x (expected 0x0000000A)", mmio_val);
            pass = 0;
        end

        // Value at offset 0x04
        mmio_val = $vpi_read_mmio(1);
        if (mmio_val == 32'hDEAD_BEEF) begin
            $display("[PASS] mmio_regs[1] = 0x%08x", mmio_val);
        end else begin
            $display("[FAIL] mmio_regs[1] = 0x%08x (expected 0xDEADBEEF)", mmio_val);
            pass = 0;
        end

        // Value at offset 0x08
        mmio_val = $vpi_read_mmio(2);
        if (mmio_val == 32'h1234_5678) begin
            $display("[PASS] mmio_regs[2] = 0x%08x", mmio_val);
        end else begin
            $display("[FAIL] mmio_regs[2] = 0x%08x (expected 0x12345678)", mmio_val);
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

        $finish;
    end

endmodule
