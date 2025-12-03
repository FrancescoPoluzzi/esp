module obi_to_esp_dma64 #(
    parameter int unsigned DATA_WIDTH = 64
)(
    input logic clk,
    input logic rst,

    // ----------------------
    // OBI Interface (Slave)
    // ----------------------
    input  obi_pkg::obi_req_t  obi_req_i,
    output obi_pkg::obi_resp_t obi_resp_o,

    // ----------------------
    // ESP DMA Interface (Master)
    // ----------------------
    // Read Control
    input  logic dma_read_ctrl_ready,
    output logic dma_read_ctrl_valid,
    output logic [31:0] dma_read_ctrl_data_index,
    output logic [31:0] dma_read_ctrl_data_length,
    output logic [2:0]  dma_read_ctrl_data_size,

    // Read Data Channel
    input  logic dma_read_chnl_valid,
    output logic dma_read_chnl_ready,
    input  logic [DATA_WIDTH-1:0] dma_read_chnl_data,

    // Write Control
    input  logic dma_write_ctrl_ready,
    output logic dma_write_ctrl_valid,
    output logic [31:0] dma_write_ctrl_data_index,
    output logic [31:0] dma_write_ctrl_data_length,
    output logic [2:0]  dma_write_ctrl_data_size,

    // Write Data Channel
    output logic dma_write_chnl_valid,
    input  logic dma_write_chnl_ready,
    output logic [DATA_WIDTH-1:0] dma_write_chnl_data
);

    // FSM States
    typedef enum logic [2:0] {
        IDLE,
        READ_CMD,
        READ_WAIT,
        WRITE_CMD,
        WRITE_DATA,
        RESP_DONE
    } state_t;

    state_t state_d, state_q;

    // Registers to hold transaction details
    logic [31:0] addr_q;
    logic [31:0] wdata_q;
    logic        addr_lsb_q; // To remember if address was aligned to upper or lower 32-bits
    logic        is_write_q;

    // Output signals logic
    logic        obi_gnt;
    logic        obi_rvalid;
    logic [31:0] obi_rdata;

    // DMA Signals
    logic        dma_rd_ctrl_vld;
    logic        dma_rd_chnl_rdy;
    logic        dma_wr_ctrl_vld;
    logic        dma_wr_chnl_vld;
    logic [63:0] dma_wr_data_out;

    // -------------------------------------------------------------------------
    // Next State & Output Logic
    // -------------------------------------------------------------------------
    always_comb begin
        // Default Assignments
        state_d = state_q;
        
        obi_gnt    = 1'b0;
        obi_rvalid = 1'b0;
        obi_rdata  = '0;

        dma_rd_ctrl_vld = 1'b0;
        dma_rd_chnl_rdy = 1'b0;
        dma_wr_ctrl_vld = 1'b0;
        dma_wr_chnl_vld = 1'b0;
        dma_wr_data_out = '0;

        case (state_q)
            IDLE: begin
                // Wait for OBI Request
                if (obi_req_i.req) begin
                    if (obi_req_i.we) begin
                        state_d = WRITE_CMD;
                    end else begin
                        state_d = READ_CMD;
                    end
                end
            end

            // -----------------------------------------------------------------
            // Read Handling
            // -----------------------------------------------------------------
            READ_CMD: begin
                dma_rd_ctrl_vld = 1'b1;
                // Wait for DMA Read Control Ready
                if (dma_read_ctrl_ready) begin
                    obi_gnt = 1'b1; // Acknowledge command to OBI
                    state_d = READ_WAIT;
                end
            end

            READ_WAIT: begin
                dma_rd_chnl_rdy = 1'b1;
                // Wait for Data from DMA
                if (dma_read_chnl_valid) begin
                    obi_rvalid = 1'b1;
                    // Select upper or lower 32 bits based on stored address LSB (bit 2)
                    // addr_lsb_q comes from addr_q[2]
                    if (addr_lsb_q) begin
                        obi_rdata = dma_read_chnl_data[63:32];
                    end else begin
                        obi_rdata = dma_read_chnl_data[31:0];
                    end
                    state_d = IDLE;
                end
            end

            // -----------------------------------------------------------------
            // Write Handling
            // -----------------------------------------------------------------
            WRITE_CMD: begin
                dma_wr_ctrl_vld = 1'b1;
                // Wait for DMA Write Control Ready
                if (dma_write_ctrl_ready) begin
                    // Note: We don't assert obi_gnt yet, we wait until data is sent too
                    // unless OBI expects gnt immediately. Here we wait to ensure atomicity.
                    state_d = WRITE_DATA;
                end
            end

            WRITE_DATA: begin
                dma_wr_chnl_vld = 1'b1;
                
                // Replicate data to both halves or steer based on address
                // Ideally DMA mask handles the byte enables, but here we mirror the data 
                // to ensure the correct lane has the payload.
                dma_wr_data_out = {wdata_q, wdata_q};

                if (dma_write_chnl_ready) begin
                    obi_gnt    = 1'b1; // Command and Data accepted
                    obi_rvalid = 1'b1; // Signal completion (Write Response)
                    state_d    = IDLE;
                end
            end

            default: state_d = IDLE;
        endcase
    end

    // -------------------------------------------------------------------------
    // Sequential Logic
    // -------------------------------------------------------------------------
    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            state_q     <= IDLE;
            addr_q      <= '0;
            wdata_q     <= '0;
            addr_lsb_q  <= 1'b0;
            is_write_q  <= 1'b0;
        end else begin
            state_q <= state_d;

            // Latch request info in IDLE when request is present
            if (state_q == IDLE && obi_req_i.req) begin
                addr_q      <= obi_req_i.addr;
                wdata_q     <= obi_req_i.wdata;
                addr_lsb_q  <= obi_req_i.addr[2]; // Bit 2 determines 32-bit alignment in 64-bit word
                is_write_q  <= obi_req_i.we;
            end
        end
    end

    // -------------------------------------------------------------------------
    // Signal Assignments
    // -------------------------------------------------------------------------
    
    // OBI Output Assignments
    assign obi_resp_o.gnt    = obi_gnt;
    assign obi_resp_o.rvalid = obi_rvalid;
    assign obi_resp_o.rdata  = obi_rdata;

    // DMA Control Assignments
    // Common length/size for simple single-beat transfers
    // Length 1, Size 4 bytes (32-bit)
    
    // Read Control
    assign dma_read_ctrl_valid       = dma_rd_ctrl_vld;
    assign dma_read_ctrl_data_index  = addr_q[31:3]; // Convert byte addr to 64-bit word index
    assign dma_read_ctrl_data_length = 32'd1;        // Single beat
    assign dma_read_ctrl_data_size   = 3'b010;       // 32-bit transfer

    // Read Channel
    assign dma_read_chnl_ready       = dma_rd_chnl_rdy;

    // Write Control
    assign dma_write_ctrl_valid       = dma_wr_ctrl_vld;
    assign dma_write_ctrl_data_index  = addr_q[31:3];
    assign dma_write_ctrl_data_length = 32'd1;
    assign dma_write_ctrl_data_size   = 3'b010;

    // Write Channel
    assign dma_write_chnl_valid       = dma_wr_chnl_vld;
    assign dma_write_chnl_data        = dma_wr_data_out;

    // Unused user signals (if any)
    // assign dma_read_ctrl_data_user = '0; 
    // assign dma_write_ctrl_data_user = '0;

endmodule