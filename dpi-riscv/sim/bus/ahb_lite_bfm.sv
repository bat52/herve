/*
 * AHB-Lite Bus Functional Model (BFM)
 *
 * Provides AHB-Lite bus transactions driven by a request/ack handshake.
 * This avoids timing controls inside DPI-exported functions, which
 * Verilator does not support.
 *
 * Usage:
 *   1. Set ahb_req_addr, ahb_req_write, ahb_req_wdata, assert ahb_req_valid
 *   2. Wait for ahb_req_ready (driven by the BFM state machine)
 *   3. Read ahb_req_rdata for read transactions
 *
 * The BFM state machine runs on HCLK and drives the AHB bus signals.
 *
 * Signals driven by this BFM:
 *   HADDR, HTRANS, HWRITE, HSIZE, HWDATA
 *
 * Signals sampled by this BFM:
 *   HREADY, HRDATA
 */

module ahb_lite_bfm (
    input  wire        HCLK,
    input  wire        HRESETn,
    // AHB-Lite master outputs
    output reg  [31:0] HADDR,
    output reg  [1:0]  HTRANS,
    output reg         HWRITE,
    output reg  [2:0]  HSIZE,
    output reg  [31:0] HWDATA,
    // AHB-Lite slave inputs
    input  wire        HREADY,
    input  wire [31:0] HRDATA,
    // Request interface (driven by DPI bridge)
    input  wire        ahb_req_valid,
    input  wire [31:0] ahb_req_addr,
    input  wire        ahb_req_write,
    input  wire [31:0] ahb_req_wdata,
    output reg         ahb_req_ready,
    output reg  [31:0] ahb_req_rdata
);

    // BFM state machine
    typedef enum logic [1:0] {
        BFM_IDLE,
        BFM_ADDR,
        BFM_DATA,
        BFM_DONE
    } bfm_state_t;

    bfm_state_t state;

    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            state         <= BFM_IDLE;
            HADDR         <= 32'h0;
            HTRANS        <= 2'b00;  // IDLE
            HWRITE        <= 1'b0;
            HSIZE         <= 3'b000;
            HWDATA        <= 32'h0;
            ahb_req_ready <= 1'b0;
            ahb_req_rdata <= 32'h0;
        end else begin
            case (state)
                BFM_IDLE: begin
                    ahb_req_ready <= 1'b0;
                    HTRANS        <= 2'b00;  // IDLE
                    if (ahb_req_valid) begin
                        // Drive address phase
                        HADDR  <= ahb_req_addr;
                        HTRANS <= 2'b10;  // NONSEQ
                        HWRITE <= ahb_req_write;
                        HSIZE  <= 3'b010; // word (4 bytes)
                        if (ahb_req_write) begin
                            HWDATA <= ahb_req_wdata;
                        end
                        state <= BFM_ADDR;
                    end
                end

                BFM_ADDR: begin
                    // Address phase complete, wait for HREADY in data phase
                    state <= BFM_DATA;
                end

                BFM_DATA: begin
                    if (HREADY) begin
                        // Data phase complete
                        if (!HWRITE) begin
                            ahb_req_rdata <= HRDATA;
                        end
                        ahb_req_ready <= 1'b1;
                        state         <= BFM_DONE;
                    end
                    // else wait for HREADY
                end

                BFM_DONE: begin
                    // Wait for requestor to de-assert valid
                    ahb_req_ready <= 1'b0;
                    if (!ahb_req_valid) begin
                        state <= BFM_IDLE;
                    end
                end
            endcase
        end
    end

endmodule
