/*
 * Top-level SV module for Zephyros Blinky Demo.
 *
 * Combines the machine timer (from tb_top_zephyr) with the actual
 * ahb_gpio DUT (from tb_top_ahb) via an AHB-Lite interconnect.
 *
 * MMIO address map:
 *   0x1000_0000  GPIO_OUT      (R/W) — via AHB-Lite GPIO module
 *   0x1000_0004  GPIO_IE       (R/W) — via AHB-Lite GPIO module
 *   0x1000_0008  GPIO_STATUS   (R)   — via AHB-Lite GPIO module
 *   0x1000_0010  MTIME_LO      (R)   — read via mtime_lo port
 *   0x1000_0014  MTIME_HI      (R)   — read via mtime_hi port
 *   0x1000_0018  MTIMECMP_LO   (R/W) — write via mtimecmp_lo port
 *   0x1000_001C  MTIMECMP_HI   (R/W) — write via mtimecmp_hi port
 *
 * Interrupt routing:
 *   - Machine timer IRQ (cause 7) → rv_set_irq(0x80)
 *   - GPIO IRQ (ext_irq from gpio_out[0]) → rv_set_irq(0x01)
 *   - Both are OR'd into the interrupt mask sent to the ISS
 *
 * Ports exposed to C++ harness:
 *   clk, rstn                      — clock/reset driven by C++
 *   ahb_req_*                      — AHB BFM request interface (for GPIO)
 *   mtime_lo, mtime_hi             — current timer value (read by C++)
 *   mtimecmp_lo, mtimecmp_hi      — timer compare value (written by C++)
 *   gpio_out, gpio_ie             — GPIO register outputs (observability)
 */

module tb_top_blinky (
    input  wire        clk,
    input  wire        rstn,
    // AHB BFM request interface (driven from C++ for GPIO MMIO)
    input  wire        ahb_req_valid,
    input  wire [31:0] ahb_req_addr,
    input  wire        ahb_req_write,
    input  wire [31:0] ahb_req_wdata,
    output wire        ahb_req_ready,
    output wire [31:0] ahb_req_rdata,
    // Timer register access (for C++ harness direct access)
    output reg  [31:0] mtime_lo,
    output reg  [31:0] mtime_hi,
    input  wire [31:0] mtimecmp_lo,
    input  wire [31:0] mtimecmp_hi,
    // GPIO register outputs (for testbench observability)
    output wire [31:0] gpio_out,
    output wire [31:0] gpio_ie
);

    // DPI import: C function to set interrupt mask in ISS
    import "DPI-C" function void rv_set_irq(int mask);

    // -------------------------------------------------------------------
    // Timer Section
    // -------------------------------------------------------------------

    // Internal 64-bit timer state
    reg [63:0] mtime;
    reg [63:0] mtimecmp;

    // mtimecmp is driven from the input ports
    always @(*) begin
        mtimecmp = {mtimecmp_hi, mtimecmp_lo};
    end

    wire mtime_ge_mtimecmp;
    assign mtime_ge_mtimecmp = (mtime >= mtimecmp);

    // Expose mtime on output ports
    always @(*) begin
        mtime_lo = mtime[31:0];
        mtime_hi = mtime[63:32];
    end

    // mtime increments on every positive clock edge
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            mtime <= 64'h0;
        end else begin
            mtime <= mtime + 64'h1;
        end
    end

    // -------------------------------------------------------------------
    // AHB-Lite GPIO Section
    // -------------------------------------------------------------------

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

    // Ext IRQ from GPIO module (driven by gpio_out[0])
    wire        ext_irq;

    // The GPIO module drives ext_irq internally from gpio_out[0]
    // We just pass it through to the IRQ routing logic.

    // Address decoding: GPIO at 0x1000_0000 - 0x1000_000F (4 registers, 16 bytes)
    assign hsel_gpio = (haddr >= 32'h1000_0000 && haddr < 32'h1000_0010);

    // AHB-Lite BFM (bus master)
    ahb_lite_bfm #() ahb_master (
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
    ahb_gpio #() dut_gpio (
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

    // GPIO ext_irq: driven by gpio_out[0] (the module does this internally,
    // but we need it for the IRQ routing below)
    assign ext_irq = gpio_out[0];

    // -------------------------------------------------------------------
    // Interrupt Routing
    // -------------------------------------------------------------------

    // Propagate interrupts to ISS via DPI on every clock edge.
    // Bit layout:
    //   bit 7 — machine timer interrupt (mtime >= mtimecmp)
    //   bit 0 — external GPIO interrupt (gpio_out[0])
    always @(posedge clk) begin
        if (!rstn) begin
            rv_set_irq(32'h0);
        end else begin
            rv_set_irq({24'h0,
                        mtime_ge_mtimecmp ? 1'b1 : 1'b0,   // bit 7: timer
                        6'h0,
                        ext_irq ? 1'b1 : 1'b0});          // bit 0: GPIO
        end
    end

endmodule
