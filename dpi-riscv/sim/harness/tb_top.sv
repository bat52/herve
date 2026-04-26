module tb_top (
    input  wire       clk,
    input  wire       rstn,
    input  wire [31:0] mem_read,
    input  wire       irq,
    output reg  [31:0] mem_write
);

    // DPI import: C function to set interrupt mask in ISS
    import "DPI-C" function void rv_set_irq(int mask);

    // DPI export: called by ISS on MMIO access
    export "DPI-C" function dpi_mmio_read;
    export "DPI-C" function dpi_mmio_write;

    function int dpi_mmio_read(int addr);
        if (addr == 32'h1000_0000) begin
            dpi_mmio_read = mem_read;
        end else if (addr == 32'h1000_0008) begin
            // GPIO_STATUS register: bit 0 = IRQ pending
            dpi_mmio_read = irq ? 32'h1 : 32'h0;
        end else begin
            dpi_mmio_read = 32'h0;
        end
    endfunction

    function void dpi_mmio_write(int addr, int data);
        if (addr == 32'h1000_0000) begin
            mem_write = data;
        end
    endfunction

    // Monitor signal changes on every clock edge
    always @(posedge clk) begin
        if (!rstn)
            $display("[SV] clk=%b rstn=%b irq=%b mem_read=0x%08x mem_write=0x%08x (RESET)",
                     clk, rstn, irq, mem_read, mem_write);
        else
            $display("[SV] clk=%b rstn=%b irq=%b mem_read=0x%08x mem_write=0x%08x",
                     clk, rstn, irq, mem_read, mem_write);
    end

    // Propagate HW IRQ line to ISS via DPI
    always @(posedge clk) begin
        if (irq)
            rv_set_irq(32'h1);
        else
            rv_set_irq(32'h0);
    end

endmodule
