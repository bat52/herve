module tb_top_mmio_regs;
    reg clk;
    reg rst;
    reg [31:0] hw_cfg_reg [0:3];

    // DPI export: called by ISS on MMIO access
    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    function int dpi_mmio_read(int addr);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0010) begin
            dpi_mmio_read = hw_cfg_reg[(addr - 32'h1000_0000) / 4];
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        if (addr >= 32'h1000_0000 && addr < 32'h1000_0010) begin
            hw_cfg_reg[(addr - 32'h1000_0000) / 4] = data;
        end
    endfunction

    initial begin
        $dumpfile("tb_top_mmio_regs.vcd");
        $dumpvars(0, tb_top_mmio_regs);
        clk = 0;
        rst = 1;
        hw_cfg_reg[0] = 32'h0;
        hw_cfg_reg[1] = 32'h0;
        hw_cfg_reg[2] = 32'h0;
        hw_cfg_reg[3] = 32'h0;
    end

endmodule
