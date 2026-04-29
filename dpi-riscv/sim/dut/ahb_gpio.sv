/*
 * AHB-Lite GPIO Slave DUT
 *
 * A simple GPIO peripheral with an AHB-Lite slave interface.
 * Register map (at configurable base address):
 *
 *   Offset  | Name         | Width | Access | Description
 *   --------|--------------|-------|--------|---------------------------
 *   0x00    | GPIO_OUT     | 32    | R/W    | Output value (bit 0 = LED / IRQ)
 *   0x04    | GPIO_IE      | 32    | R/W    | Interrupt enable
 *   0x08    | GPIO_STATUS  | 32    | R      | Interrupt status (bit 0 = pending)
 *   0x0C    | GPIO_RESERVED| 32    | R      | Reserved (reads 0)
 *
 * AHB-Lite interface:
 *   - Single-cycle HREADY (combinational decode)
 *   - Word-aligned accesses only (HSIZE=010)
 *   - Address decoding via HSEL signal
 */

module ahb_gpio (
    input  wire        HCLK,
    input  wire        HRESETn,
    // AHB-Lite slave interface
    input  wire        HSEL,
    input  wire [31:0] HADDR,
    input  wire [1:0]  HTRANS,
    input  wire        HWRITE,
    input  wire [2:0]  HSIZE,
    input  wire [31:0] HWDATA,
    output reg         HREADY,
    output reg  [31:0] HRDATA,
    // External interrupt line (driven by testbench)
    input  wire        ext_irq,
    // Exposed register outputs (for testbench observability / IRQ routing)
    output wire [31:0] gpio_out,
    output wire [31:0] gpio_ie
);

    // Register storage
    reg [31:0] gpio_out_reg;
    reg [31:0] gpio_ie_reg;

    // Internal signals
    wire        access_valid;
    wire [3:0]  byte_offset;

    assign access_valid = HSEL && (HTRANS == 2'b10 || HTRANS == 2'b11);
    assign byte_offset  = HADDR[3:0];

    // Expose register values as outputs
    assign gpio_out = gpio_out_reg;
    assign gpio_ie  = gpio_ie_reg;

    // Always ready (combinational — single-cycle response)
    always @(*) begin
        HREADY = 1'b1;
    end

    // Read: combinational decode
    always @(*) begin
        if (HSEL && !HWRITE && (HTRANS == 2'b10 || HTRANS == 2'b11)) begin
            case (byte_offset)
                4'h0: HRDATA = gpio_out_reg;
                4'h4: HRDATA = gpio_ie_reg;
                4'h8: HRDATA = {31'h0, ext_irq};  // GPIO_STATUS = ext_irq
                default: HRDATA = 32'h0;
            endcase
        end else begin
            HRDATA = 32'h0;
        end
    end

    // Write: sequential (on posedge HCLK)
    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            gpio_out_reg <= 32'h0;
            gpio_ie_reg  <= 32'h0;
        end else if (access_valid && HWRITE) begin
            case (byte_offset)
                4'h0: gpio_out_reg <= HWDATA;
                4'h4: gpio_ie_reg  <= HWDATA;
                default: begin end // writes to STATUS or reserved are ignored
            endcase
        end
    end

endmodule
