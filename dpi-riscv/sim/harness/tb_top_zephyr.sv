/*
 * Top-level SV module for Zephyr testbench with machine timer.
 *
 * This module extends tb_top.sv with a RISC-V machine timer:
 *   - 64-bit mtime counter (increments on each posedge clk)
 *   - 64-bit mtimecmp compare register (writeable via MMIO)
 *   - timer_irq asserted when mtime >= mtimecmp
 *   - Machine timer interrupt (IRQ cause 7) sent to ISS via rv_set_irq(0x80)
 *
 * Timer registers are exposed as ports so the C++ test harness can
 * access them directly (bypassing DPI export dispatch):
 *   - mtime_lo/mtime_hi  — output, read current mtime
 *   - mtimecmp_lo/mtimecmp_hi — input, set by C++ on timer MMIO writes
 *
 * MMIO address map:
 *   0x1000_0000  GPIO_OUT      (R/W)
 *   0x1000_0008  GPIO_STATUS   (R)
 *   0x1000_0010  MTIME_LO      (R)   — read via mtime_lo port
 *   0x1000_0014  MTIME_HI      (R)   — read via mtime_hi port
 *   0x1000_0018  MTIMECMP_LO   (R/W) — write via mtimecmp_lo port
 *   0x1000_001C  MTIMECMP_HI   (R/W) — write via mtimecmp_hi port
 */

module tb_top_zephyr (
    input  wire       clk,
    input  wire       rstn,
    input  wire [31:0] mem_read,
    input  wire       irq,          // external GPIO IRQ (bit 0)
    output reg  [31:0] mem_write,
    // Timer register access (for C++ harness direct access)
    output reg  [31:0] mtime_lo,
    output reg  [31:0] mtime_hi,
    input  wire [31:0] mtimecmp_lo,  // driven by C++ when firmware writes MTIMECMP
    input  wire [31:0] mtimecmp_hi
);

    // DPI import: C function to set interrupt mask in ISS
    import "DPI-C" function void rv_set_irq(int mask);

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

    // DPI MMIO write: called from ISS C code (handles GPIO only)
    function void dpi_mmio_write(int addr, int data);
        if (addr == 32'h1000_0000) begin
            mem_write = data;
        end
    endfunction

    // Propagate interrupts to ISS via DPI on every clock edge
    always @(posedge clk) begin
        if (!rstn) begin
            rv_set_irq(32'h0);
        end else begin
            rv_set_irq({24'h0, mtime_ge_mtimecmp ? 1'b1 : 1'b0, 6'h0, irq ? 1'b1 : 1'b0});
        end
    end

endmodule
