module tb_top;
    reg clk;
    reg rst;
    reg [31:0] mem_read;
    reg [31:0] mem_write;

    // DPI import: C function to load firmware binary into ISS
    import "DPI-C" function void tb_load_firmware(string firmware_path);

    // DPI export: called by ISS on MMIO access
    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    function int dpi_mmio_read(int addr);
        if (addr == 32'h1000_0000) begin
            dpi_mmio_read = mem_read;
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        if (addr == 32'h1000_0000) begin
            mem_write = data;
        end
    endfunction

    initial begin
        $dumpfile("tb_top.vcd");
        $dumpvars(0, tb_top);
        clk = 0;
        rst = 1;
        mem_read = 32'h0;
        mem_write = 32'h0;

        // Load firmware at start of sim
        tb_load_firmware("firmware.bin");
    end

endmodule

