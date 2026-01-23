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
    output logic [5:0]  dma_read_ctrl_data_user,

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
    output logic [5:0]  dma_write_ctrl_data_user,

    // Write Data Channel
    output logic dma_write_chnl_valid,
    input  logic dma_write_chnl_ready,
    output logic [DATA_WIDTH-1:0] dma_write_chnl_data
);

    localparam logic [31:0] EXT_SLAVE_BASE = 32'hF000_0000;
    localparam logic [2:0]  DMA_SIZE_DWORD = 3'b011; // 64-bit

    // FSM States
    typedef enum logic [3:0] {
        IDLE,
        READ_CMD,
        READ_WAIT,
        RMW_READ_CMD,
        RMW_READ_WAIT,
        WRITE_CMD,
        WRITE_DATA
    } state_t;

    state_t state_d, state_q;

    // Registers
    logic [31:0] addr_q;
    logic [31:0] wdata_q;
    logic [3:0]  be_q;
    logic        addr_lsb_q;
    logic [63:0] rmw_data_q; 
    logic        is_p2p_q;   
    
    // Output signals logic
    logic        obi_gnt;
    logic        obi_rvalid;
    logic [31:0] obi_rdata;

    // DMA Signals
    logic        dma_rd_ctrl_vld;
    logic        dma_rd_chnl_rdy;
    logic        dma_wr_ctrl_vld;
    logic        dma_wr_chnl_vld;
    logic [DATA_WIDTH-1:0] dma_wr_data_out;

    // Address Translation
    logic [5:0]  dma_user_field;
    logic [31:0] dma_index_addr;

    assign dma_user_field = addr_q[27:22];
    assign dma_index_addr = {10'b0, addr_q[21:0]}; 

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
                if (obi_req_i.req) begin
                    if (obi_req_i.we) begin
                        if (obi_req_i.addr[27:22] == 6'b0) begin
                            state_d = RMW_READ_CMD;
                        end else begin
                            state_d = WRITE_CMD;
                        end
                    end else begin
                        state_d = READ_CMD;
                    end
                end
            end

            // -----------------------------------------------------------------
            // Standard Read
            // -----------------------------------------------------------------
            READ_CMD: begin
                dma_rd_ctrl_vld = 1'b1;
                if (dma_read_ctrl_ready) begin
                    obi_gnt = 1'b1; 
                    state_d = READ_WAIT;
                end
            end

            READ_WAIT: begin
                dma_rd_chnl_rdy = 1'b1;
                if (dma_read_chnl_valid) begin
                    obi_rvalid = 1'b1;
                    obi_rdata = addr_lsb_q ? dma_read_chnl_data[63:32] : dma_read_chnl_data[31:0];
                    state_d = IDLE;
                end
            end

            // -----------------------------------------------------------------
            // RMW Read Phase
            // -----------------------------------------------------------------
            RMW_READ_CMD: begin
                dma_rd_ctrl_vld = 1'b1;
                if (dma_read_ctrl_ready) begin
                    state_d = RMW_READ_WAIT;
                end
            end

            RMW_READ_WAIT: begin
                dma_rd_chnl_rdy = 1'b1;
                if (dma_read_chnl_valid) begin
                    state_d = WRITE_CMD;
                end
            end

            // -----------------------------------------------------------------
            // Write Phase
            // -----------------------------------------------------------------
            WRITE_CMD: begin
                dma_wr_ctrl_vld = 1'b1;
                if (dma_write_ctrl_ready) begin
                    obi_gnt = 1'b1;
                    state_d = WRITE_DATA;
                end
            end

            WRITE_DATA: begin
                dma_wr_chnl_vld = 1'b1;
                
                if (is_p2p_q) begin
                    // Direct Write (P2P)
                    if (addr_lsb_q) begin
                        dma_wr_data_out = {wdata_q, 32'b0};
                    end else begin
                        dma_wr_data_out = {32'b0, wdata_q};
                    end
                end else begin
                    // RMW Merge (Memory)
                    dma_wr_data_out = rmw_data_q;
                    if (addr_lsb_q) begin
                        // Upper 32 bits
                        if (be_q[0]) dma_wr_data_out[39:32] = wdata_q[7:0];
                        if (be_q[1]) dma_wr_data_out[47:40] = wdata_q[15:8];
                        if (be_q[2]) dma_wr_data_out[55:48] = wdata_q[23:16];
                        if (be_q[3]) dma_wr_data_out[63:56] = wdata_q[31:24];
                    end else begin
                        // Lower 32 bits
                        if (be_q[0]) dma_wr_data_out[7:0]   = wdata_q[7:0];
                        if (be_q[1]) dma_wr_data_out[15:8]  = wdata_q[15:8];
                        if (be_q[2]) dma_wr_data_out[23:16] = wdata_q[23:16];
                        if (be_q[3]) dma_wr_data_out[31:24] = wdata_q[31:24];
                    end
                end

                if (dma_write_chnl_ready) begin
                    obi_rvalid = 1'b1; 
                    state_d    = IDLE;
                end
            end

            default: state_d = IDLE;
        endcase
    end

    // -------------------------------------------------------------------------
    // Sequential Logic
    // -------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst) begin
        if (!rst) begin
            state_q     <= IDLE;
            addr_q      <= '0;
            wdata_q     <= '0;
            be_q        <= '0;
            addr_lsb_q  <= 1'b0;
            rmw_data_q  <= '0;
            is_p2p_q    <= 1'b0;
        end else begin
            state_q <= state_d;

            // Latch Request info in IDLE
            if (state_q == IDLE && obi_req_i.req) begin
                addr_q      <= obi_req_i.addr;
                wdata_q     <= obi_req_i.wdata;
                be_q        <= obi_req_i.be;
                addr_lsb_q  <= obi_req_i.addr[2];
                if (obi_req_i.addr[27:22] != 6'b0) begin
                    is_p2p_q <= 1'b1;
                end else begin
                    is_p2p_q <= 1'b0;
                end
            end
            
            // Capture RMW Read Data
            if (state_q == RMW_READ_WAIT && dma_read_chnl_valid) begin
                rmw_data_q <= dma_read_chnl_data;
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
    assign dma_read_ctrl_valid       = dma_rd_ctrl_vld;
    assign dma_read_ctrl_data_index  = dma_index_addr[31:3]; 
    assign dma_read_ctrl_data_length = 32'd1;        
    assign dma_read_ctrl_data_size   = DMA_SIZE_DWORD;
    assign dma_read_ctrl_data_user   = dma_user_field;

    assign dma_read_chnl_ready       = dma_rd_chnl_rdy;

    assign dma_write_ctrl_valid       = dma_wr_ctrl_vld;
    assign dma_write_ctrl_data_index  = dma_index_addr[31:3];
    assign dma_write_ctrl_data_length = 32'd1;
    assign dma_write_ctrl_data_size   = DMA_SIZE_DWORD;
    assign dma_write_ctrl_data_user   = dma_user_field;

    assign dma_write_chnl_valid       = dma_wr_chnl_vld;
    assign dma_write_chnl_data        = dma_wr_data_out;

endmodule
