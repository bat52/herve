/*
 * Top-level SV module for AHB-Lite DPI testbench.
 *
 * This module:
 *   1. Instantiates the AHB-Lite BFM (bus master)
 *   2. Instantiates the AHB-Lite GPIO DUT (bus slave)
 *
 * The BFM's request/ack handshake interface is exposed as top-level ports
 * so the C++ test harness can drive them via direct model access.
 *
 * The C++ testbench drives clk and rstn, and the ISS calls
 * dpi_mmio_read/dpi_mmio_write which drive the BFM and tick the clock.
 *
 * Interrupt signal ext_irq is generated in Verilog from gpio_out[0],
 * emulating a real peripheral assertion of the interrupt line.
 * On each posedge clk, the DPI function rv_set_irq() is called to
 * propagate the IRQ to the ISS.
 */

module tb_top_ahb (
    input  wire        clk,
    input  wire        rstn,
    // BFM request interface (driven from C++ harness)
    input  wire        ahb_req_valid,
    input  wire [31:0] ahb_req_addr,
    input  wire        ahb_req_write,
    input  wire [31:0] ahb_req_wdata,
    output wire        ahb_req_ready,
    output wire [31:0] ahb_req_rdata,
    // DUT register outputs (for testbench observability)
    output wire [31:0] gpio_out,
    output wire [31:0] gpio_ie
);

    // DPI import: C function to set interrupt mask in ISS
    import "DPI-C" function void rv_set_irq(int mask);

    // AHB-Lite interconnect signals
    wire [31:0] haddr;
    wire [1:0]  htrans;
    wire        hwrite;
    wire [2:0]  hsize;
    wire [31:0] hwdata;
    wire        hready;
    wire [31:0] hrdata;

    // GPIO slave select (decoded from address)
    wire        hsel_gpio;

    // Interrupt signal: driven by gpio_out[0] (Verilog process)
    wire        ext_irq;

    assign ext_irq = gpio_out[0];

    // Propagate IRQ to ISS via DPI on every clock edge
    always @(posedge clk) begin
        if (ext_irq)
            rv_set_irq(32'h1);
        else
            rv_set_irq(32'h0);
    end

    // Address decoding: GPIO at 0x1000_0000 - 0x1000_000F (4 registers, 16 bytes)
    assign hsel_gpio = (haddr >= 32'h1000_0000 && haddr < 32'h1000_0010);

    // AHB-Lite BFM (bus master)
    ahb_lite_bfm ahb_master (
        .HCLK(clk),
        .HRESETn(rstn),
        .HADDR(haddr),
        .HTRANS(htrans),
        .HWRITE(hwrite),
        .HSIZE(hsize),
        .HWDATA(hwdata),
        .HREADY(hready),
        .HRDATA(hrdata),
        .ahb_req_valid(ahb_req_valid),
        .ahb_req_addr(ahb_req_addr),
        .ahb_req_write(ahb_req_write),
        .ahb_req_wdata(ahb_req_wdata),
        .ahb_req_ready(ahb_req_ready),
        .ahb_req_rdata(ahb_req_rdata)
    );

    // AHB-Lite GPIO DUT (bus slave)
    ahb_gpio dut_gpio (
        .HCLK(clk),
        .HRESETn(rstn),
        .HSEL(hsel_gpio),
        .HADDR(haddr),
        .HTRANS(htrans),
        .HWRITE(hwrite),
        .HSIZE(hsize),
        .HWDATA(hwdata),
        .HREADY(hready),
        .HRDATA(hrdata),
        .ext_irq(ext_irq),
        .gpio_out(gpio_out),
        .gpio_ie(gpio_ie)
    );

endmodule
