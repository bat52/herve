/*
 * RISC-V Machine Timer (rv_timer)
 *
 * Implements the standard RISC-V machine timer (mtime/mtimecmp) as a
 * memory-mapped peripheral. The 64-bit mtime counter increments on
 * every positive clock edge. When mtime >= mtimecmp, the timer_irq
 * output is asserted.
 *
 * Register map (word-aligned, 4-byte spaced at relative offsets):
 *
 *   Offset | Name        | Width | Access | Description
 *   -------|-------------|-------|--------|------------------------------
 *   0x00   | MTIME_LO    | 32    | R      | Lower 32 bits of mtime
 *   0x04   | MTIME_HI    | 32    | R      | Upper 32 bits of mtime
 *   0x08   | MTIMECMP_LO | 32    | R/W    | Lower 32 bits of mtimecmp
 *   0x0C   | MTIMECMP_HI | 32    | R/W    | Upper 32 bits of mtimecmp
 *
 * Interface:
 *   - Simple register-select: 2-bit address selects one of 4 registers
 *   - reg_re / reg_we strobes for read and write
 *   - Combinational read (reg_rdata valid in same cycle as reg_re)
 *   - Sequential write (registers update on posedge clk when reg_we)
 */

module rv_timer (
    input  wire        clk,
    input  wire        rstn,
    // Register interface
    input  wire [1:0]  reg_addr,   // 2-bit address: 0,1,2,3
    input  wire        reg_re,     // read enable
    input  wire        reg_we,     // write enable
    input  wire [31:0] reg_wdata,  // write data
    output reg  [31:0] reg_rdata,  // read data
    // Interrupt output
    output reg         timer_irq
);

    // 64-bit mtime counter and compare register
    reg [63:0] mtime;
    reg [63:0] mtimecmp;

    // Internal compare result (combinational)
    wire mtime_ge_mtimecmp;

    assign mtime_ge_mtimecmp = (mtime >= mtimecmp);

    // ---- mtime increment on every clock cycle ----
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            mtime <= 64'h0;
        end else begin
            mtime <= mtime + 64'h1;
        end
    end

    // ---- mtimecmp write (sequential) ----
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            mtimecmp <= {64{1'b1}};  // max value — no interrupt until set
        end else if (reg_we) begin
            case (reg_addr)
                2'b10: mtimecmp[31:0]  <= reg_wdata;  // MTIMECMP_LO
                2'b11: mtimecmp[63:32] <= reg_wdata;  // MTIMECMP_HI
                default: ;  // MTIME_LO / MTIME_HI are read-only
            endcase
        end
    end

    // ---- Read (combinational) ----
    always @(*) begin
        if (reg_re) begin
            case (reg_addr)
                2'b00:   reg_rdata = mtime[31:0];       // MTIME_LO
                2'b01:   reg_rdata = mtime[63:32];      // MTIME_HI
                2'b10:   reg_rdata = mtimecmp[31:0];    // MTIMECMP_LO
                2'b11:   reg_rdata = mtimecmp[63:32];   // MTIMECMP_HI
                default: reg_rdata = 32'h0;
            endcase
        end else begin
            reg_rdata = 32'h0;
        end
    end

    // ---- Timer interrupt output ----
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            timer_irq <= 1'b0;
        end else begin
            timer_irq <= mtime_ge_mtimecmp;
        end
    end

endmodule
