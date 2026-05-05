/*
 * tb_modelsim_mmio_regs.sv — MMIO register config test for ModelSim.
 *
 * Pure-SystemVerilog DPI-C testbench that loads firmware_mmio_regs.bin
 * into the Herve ISS, executes it, and verifies that four MMIO registers
 * were written with the expected values.
 *
 * Build & run:
 *   cd dpi-riscv
 *   gcc -shared -fPIC -I./sim/iss -o sim/modelsim/rv32_dpi_mti.so \
 *       sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c
 *   cd sim/modelsim
 *   vlib work
 *   vlog tb_modelsim_mmio_regs.sv
 *   vsim -c -sv_lib rv32_dpi_mti -do "run -all; quit" tb_modelsim_mmio_regs
 *
 * Expected output:
 *   [PASS] mmio_regs[0] = 0xAAAAAAAB
 *   [PASS] mmio_regs[1] = 0x000005A5
 *   [PASS] mmio_regs[2] = 0x55555554
 *   [PASS] mmio_regs[3] = 0x000007FF
 *   RESULT: PASS
 */

module tb_modelsim_mmio_regs;

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
        $display("  ModelSim DPI-C MMIO Regs Test");
        $display("========================================");
        $display("");

        // Load firmware into ISS
        rv_init("firmware_mmio_regs.bin", 1 << 20);
        rv_reset(0);
        $display("ISS initialized, RAM = 1 MiB");

        // Execute firmware
        $display("Executing firmware...");
        executed = rv_step(32);
        $display("rv_step: executed %0d instructions", executed);
        $display("PC after execution: 0x%08x", rv_get_pc());

        // ================================================================
        // Verify results
        //
        // firmware_mmio_regs.S writes:
        //   reg[0] = 0xAAAA_AAAB
        //   reg[1] = 0x0000_05A5
        //   reg[2] = 0x5555_5554
        //   reg[3] = 0x0000_07FF
        // ================================================================

        pass = 1;

        $display("");
        $display("--- Verification ---");

        if (mmio_regs[0] == 32'hAAAA_AAAB) begin
            $display("[PASS] mmio_regs[0] = 0x%08x", mmio_regs[0]);
        end else begin
            $display("[FAIL] mmio_regs[0] = 0x%08x (expected 0xAAAAAAAB)", mmio_regs[0]);
            pass = 0;
        end

        if (mmio_regs[1] == 32'h0000_05A5) begin
            $display("[PASS] mmio_regs[1] = 0x%08x", mmio_regs[1]);
        end else begin
            $display("[FAIL] mmio_regs[1] = 0x%08x (expected 0x000005A5)", mmio_regs[1]);
            pass = 0;
        end

        if (mmio_regs[2] == 32'h5555_5554) begin
            $display("[PASS] mmio_regs[2] = 0x%08x", mmio_regs[2]);
        end else begin
            $display("[FAIL] mmio_regs[2] = 0x%08x (expected 0x55555554)", mmio_regs[2]);
            pass = 0;
        end

        if (mmio_regs[3] == 32'h0000_07FF) begin
            $display("[PASS] mmio_regs[3] = 0x%08x", mmio_regs[3]);
        end else begin
            $display("[FAIL] mmio_regs[3] = 0x%08x (expected 0x000007FF)", mmio_regs[3]);
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
