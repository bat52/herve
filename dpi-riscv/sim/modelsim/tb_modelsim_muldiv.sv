/*
 * tb_modelsim_muldiv.sv — MUL/DIV extension test for ModelSim.
 *
 * Pure-SystemVerilog DPI-C testbench that loads firmware_muldiv.bin
 * into the Herve ISS, executes it, and verifies 16 MUL/DIV/REM results.
 *
 * Build & run:
 *   cd dpi-riscv
 *   gcc -shared -fPIC -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
 *       sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
 *   cd sim/modelsim
 *   vlib work
 *   vlog tb_modelsim_muldiv.sv
 *   vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_muldiv
 *
 * Expected output:
 *   [PASS] reg[0]  (MUL)        = 21
 *   [PASS] reg[1]  (MULH)       = 0x014B66DC
 *   ...
 *   RESULT: PASS
 */

module tb_modelsim_muldiv;

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
    // Test sequence
    // ====================================================================
    initial begin
        integer executed;
        integer pass;

        $display("========================================");
        $display("  ModelSim DPI-C MUL/DIV Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        rv_init("firmware_muldiv.bin", 1 << 20);
        rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Execute firmware
        $display("Executing firmware...");
        executed = rv_step(200);
        $display("rv_step: executed %0d instructions", executed);
        $display("PC after execution: 0x%08x", rv_get_pc());

        // ================================================================
        // Verify results
        //
        // firmware_muldiv.S writes 16 results to regs[0..15]:
        //   reg[0]  = MUL:        7 * 3 = 21
        //   reg[1]  = MULH:       0x12345678 * 0x12345678 -> upper = 0x014B66DC
        //   reg[2]  = MULHSU:     -1000 * 2000000 -> upper
        //   reg[3]  = MULHU:      0xFFFFFFFF * 0xFFFFFFFF -> upper = 0xFFFFFFFE
        //   reg[4]  = DIV:        100 / 3 = 33
        //   reg[5]  = DIVU:       200 / 7 = 28
        //   reg[6]  = REM:        100 % 3 = 1
        //   reg[7]  = REMU:       200 % 7 = 4
        //   reg[8]  = DIV/0:      50 / 0 = 0xFFFFFFFF
        //   reg[9]  = DIVU/0:     50 / 0 = 0xFFFFFFFF
        //   reg[10] = REM/0:      50 % 0 = 50
        //   reg[11] = REMU/0:     50 % 0 = 50
        //   reg[12] = INT32_MIN / -1 = 0x80000000
        //   reg[13] = INT32_MIN % -1 = 0
        //   reg[14] = MUL large:  0x10000001 * 0x10000001 = 0x00000001
        //   reg[15] = MULH large: 0x10000001 * 0x10000001 -> upper = 0x01000000
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        // Test 1: MUL
        if (mmio_regs[0] == 32'd21) begin
            $display("[PASS] reg[0]  (MUL)        = %0d", mmio_regs[0]);
        end else begin
            $display("[FAIL] reg[0]  (MUL)        = %0d (expected 21)", mmio_regs[0]);
            pass = 0;
        end

        // Test 2: MULH
        if (mmio_regs[1] == 32'h014B_66DC) begin
            $display("[PASS] reg[1]  (MULH)       = 0x%08X", mmio_regs[1]);
        end else begin
            $display("[FAIL] reg[1]  (MULH)       = 0x%08X (expected 0x014B66DC)", mmio_regs[1]);
            pass = 0;
        end

        // Test 3: MULHSU
        if (mmio_regs[2] == 32'hFFFF_FFFF) begin
            $display("[PASS] reg[2]  (MULHSU)     = 0x%08X", mmio_regs[2]);
        end else begin
            $display("[FAIL] reg[2]  (MULHSU)     = 0x%08X (expected 0xFFFFFFFF)", mmio_regs[2]);
            pass = 0;
        end

        // Test 4: MULHU
        if (mmio_regs[3] == 32'hFFFF_FFFE) begin
            $display("[PASS] reg[3]  (MULHU)      = 0x%08X", mmio_regs[3]);
        end else begin
            $display("[FAIL] reg[3]  (MULHU)      = 0x%08X (expected 0xFFFFFFFE)", mmio_regs[3]);
            pass = 0;
        end

        // Test 5: DIV
        if (mmio_regs[4] == 32'd33) begin
            $display("[PASS] reg[4]  (DIV)        = %0d", mmio_regs[4]);
        end else begin
            $display("[FAIL] reg[4]  (DIV)        = %0d (expected 33)", mmio_regs[4]);
            pass = 0;
        end

        // Test 6: DIVU
        if (mmio_regs[5] == 32'd28) begin
            $display("[PASS] reg[5]  (DIVU)       = %0d", mmio_regs[5]);
        end else begin
            $display("[FAIL] reg[5]  (DIVU)       = %0d (expected 28)", mmio_regs[5]);
            pass = 0;
        end

        // Test 7: REM
        if (mmio_regs[6] == 32'd1) begin
            $display("[PASS] reg[6]  (REM)        = %0d", mmio_regs[6]);
        end else begin
            $display("[FAIL] reg[6]  (REM)        = %0d (expected 1)", mmio_regs[6]);
            pass = 0;
        end

        // Test 8: REMU
        if (mmio_regs[7] == 32'd4) begin
            $display("[PASS] reg[7]  (REMU)       = %0d", mmio_regs[7]);
        end else begin
            $display("[FAIL] reg[7]  (REMU)       = %0d (expected 4)", mmio_regs[7]);
            pass = 0;
        end

        // Test 9: DIV by zero
        if (mmio_regs[8] == 32'hFFFF_FFFF) begin
            $display("[PASS] reg[8]  (DIV/0)      = 0x%08X", mmio_regs[8]);
        end else begin
            $display("[FAIL] reg[8]  (DIV/0)      = 0x%08X (expected 0xFFFFFFFF)", mmio_regs[8]);
            pass = 0;
        end

        // Test 10: DIVU by zero
        if (mmio_regs[9] == 32'hFFFF_FFFF) begin
            $display("[PASS] reg[9]  (DIVU/0)     = 0x%08X", mmio_regs[9]);
        end else begin
            $display("[FAIL] reg[9]  (DIVU/0)     = 0x%08X (expected 0xFFFFFFFF)", mmio_regs[9]);
            pass = 0;
        end

        // Test 11: REM by zero
        if (mmio_regs[10] == 32'd50) begin
            $display("[PASS] reg[10] (REM/0)      = %0d", mmio_regs[10]);
        end else begin
            $display("[FAIL] reg[10] (REM/0)      = %0d (expected 50)", mmio_regs[10]);
            pass = 0;
        end

        // Test 12: REMU by zero
        if (mmio_regs[11] == 32'd50) begin
            $display("[PASS] reg[11] (REMU/0)     = %0d", mmio_regs[11]);
        end else begin
            $display("[FAIL] reg[11] (REMU/0)     = %0d (expected 50)", mmio_regs[11]);
            pass = 0;
        end

        // Test 13: INT32_MIN / -1
        if (mmio_regs[12] == 32'h8000_0000) begin
            $display("[PASS] reg[12] (MIN/-1)     = 0x%08X", mmio_regs[12]);
        end else begin
            $display("[FAIL] reg[12] (MIN/-1)     = 0x%08X (expected 0x80000000)", mmio_regs[12]);
            pass = 0;
        end

        // Test 14: INT32_MIN % -1
        if (mmio_regs[13] == 32'd0) begin
            $display("[PASS] reg[13] (MIN%%-1)     = %0d", mmio_regs[13]);
        end else begin
            $display("[FAIL] reg[13] (MIN%%-1)     = %0d (expected 0)", mmio_regs[13]);
            pass = 0;
        end

        // Test 15: MUL large
        if (mmio_regs[14] == 32'h0000_0001) begin
            $display("[PASS] reg[14] (MUL large)  = 0x%08X", mmio_regs[14]);
        end else begin
            $display("[FAIL] reg[14] (MUL large)  = 0x%08X (expected 0x00000001)", mmio_regs[14]);
            pass = 0;
        end

        // Test 16: MULH large
        if (mmio_regs[15] == 32'h0100_0000) begin
            $display("[PASS] reg[15] (MULH large) = 0x%08X", mmio_regs[15]);
        end else begin
            $display("[FAIL] reg[15] (MULH large) = 0x%08X (expected 0x01000000)", mmio_regs[15]);
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
