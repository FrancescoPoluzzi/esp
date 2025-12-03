module xheep_boot_controller_dma64 (
    input  logic        clk,
    input  logic        rst_n,

    // Configuration Triggers
    input  logic        trigger_fetch,      // From conf_info_boot_fetch_code
    input  logic [31:0] fetch_addr_byte,    // From conf_info_boot_fetch_code_addr (Byte Address)
    input  logic [31:0] fetch_size_words,   // From conf_info_code_size_words
    input  logic        trigger_boot_exit,  // From conf_info_boot_exit_loop

    // DMA Read Control (Master)
    output logic        dma_read_ctrl_valid,
    input  logic        dma_read_ctrl_ready,
    output logic [31:0] dma_read_ctrl_data_index,
    output logic [31:0] dma_read_ctrl_data_length,
    output logic [2:0]  dma_read_ctrl_data_size,

    // DMA Read Channel (Slave)
    input  logic        dma_read_chnl_valid,
    output logic        dma_read_chnl_ready,
    input  logic [31:0] dma_read_chnl_data,

    // OBI Master (To X-HEEP External Slave Port)
    output obi_pkg::obi_req_t  obi_req_o,
    input  obi_pkg::obi_resp_t obi_resp_i,

    // Status
    output logic        busy,
    output logic        fetch_done_o        // Pulses when a DMA Fetch operation completes
);

    // X-HEEP Memory Map Constants
    localparam logic [31:0] XHEEP_RAM_START_ADDR = 32'h0000_0000;
    localparam logic [31:0] XHEEP_SOC_CTRL_ADDR  = 32'h2000_000c; // Base 0x20000000 + Offset 0x0C (BOOT_EXIT_LOOP)

    // DMA Constants
    localparam logic [2:0]  DMA_SIZE_WORD = 3'b010; // 32-bit

    // State Machine
    typedef enum logic [2:0] {
        IDLE,
        // Fetch Sequence
        DMA_REQ,        // Send DMA Read Request to ESP
        DMA_WAIT_DATA,  // Receive data from ESP and Write to X-HEEP
        // Boot Exit Sequence
        EXIT_WRITE      // Write to SOC_CTRL
    } state_t;

    state_t state_d, state_q;

    // Registers
    logic [31:0] current_ram_addr_d, current_ram_addr_q;
    logic        prev_trigger_fetch_q;
    logic        prev_trigger_exit_q;
    logic [31:0] beat_cnt_d, beat_cnt_q;

    // Edge Detection
    logic rise_fetch;
    logic rise_exit;

    // Since ESP resets the accelerator between runs, prev_trigger_q will be 0 after reset.
    // If the config bit is set to 1 by SW, we will detect a rising edge immediately.
    assign rise_fetch = trigger_fetch && !prev_trigger_fetch_q;
    assign rise_exit  = trigger_boot_exit && !prev_trigger_exit_q;

    // Output logic
    always_comb begin
        // Defaults
        state_d = state_q;
        current_ram_addr_d = current_ram_addr_q;
        beat_cnt_d = beat_cnt_q;
        
        busy = 1'b0;
        fetch_done_o = 1'b0;

        // DMA Defaults
        dma_read_ctrl_valid = 1'b0;
        dma_read_ctrl_data_index = '0;
        dma_read_ctrl_data_length = '0;
        dma_read_ctrl_data_size = DMA_SIZE_WORD;
        dma_read_chnl_ready = 1'b0;

        // OBI Defaults
        obi_req_o.req = 1'b0;
        obi_req_o.we = 1'b0;
        obi_req_o.be = 4'b1111;
        obi_req_o.addr = '0;
        obi_req_o.wdata = '0;

        case (state_q)
            IDLE: begin
                if (rise_fetch) begin
                    state_d = DMA_REQ;
                    current_ram_addr_d = XHEEP_RAM_START_ADDR;
                    beat_cnt_d = 0;
                    busy = 1'b1;
                end else if (rise_exit) begin
                    state_d = EXIT_WRITE;
                    busy = 1'b1;
                end
            end

            // ----------------------------------------------------------------
            // CODE FETCH SEQUENCE
            // ----------------------------------------------------------------
            DMA_REQ: begin
                busy = 1'b1;
                dma_read_ctrl_valid = 1'b1;
                // ESP DMA Index is in Beats (Words). Convert byte address to word index.
                dma_read_ctrl_data_index = fetch_addr_byte[31:2];
                dma_read_ctrl_data_length = fetch_size_words; 
                dma_read_ctrl_data_size = DMA_SIZE_WORD;

                if (dma_read_ctrl_ready) begin
                    state_d = DMA_WAIT_DATA;
                end
            end

            DMA_WAIT_DATA: begin
                busy = 1'b1;
                
                // Flow control: Only accept DMA data if X-HEEP OBI accepts our write
                if (dma_read_chnl_valid) begin
                    // Prepare OBI Write to X-HEEP RAM
                    obi_req_o.req = 1'b1;
                    obi_req_o.we  = 1'b1;
                    obi_req_o.addr = current_ram_addr_q;
                    obi_req_o.wdata = dma_read_chnl_data;
                    obi_req_o.be    = 4'b1111;

                    if (obi_resp_i.gnt) begin
                        // Write accepted by X-HEEP, Ack the DMA beat
                        dma_read_chnl_ready = 1'b1;
                        
                        // Increment internal RAM address
                        current_ram_addr_d = current_ram_addr_q + 4;
                        
                        // Count beats
                        beat_cnt_d = beat_cnt_q + 1;
                        
                        // Check for completion
                        if (beat_cnt_d == fetch_size_words) begin
                            state_d = IDLE;
                            fetch_done_o = 1'b1; // SIGNAL COMPLETION FOR FETCH
                        end
                    end
                end
            end

            // ----------------------------------------------------------------
            // BOOT EXIT SEQUENCE
            // ----------------------------------------------------------------
            EXIT_WRITE: begin
                busy = 1'b1;
                // Write 1 to BOOT_EXIT_LOOP register
                obi_req_o.req = 1'b1;
                obi_req_o.we  = 1'b1;
                obi_req_o.addr = XHEEP_SOC_CTRL_ADDR;
                obi_req_o.wdata = 32'd1; 
                obi_req_o.be    = 4'b1111;

                if (obi_resp_i.gnt) begin
                    state_d = IDLE;
                    // Note: We do NOT raise fetch_done_o here. 
                    // We want acc_done to remain low until X-HEEP finishes execution (heep_exit_valid).
                end
            end
        endcase
    end

    // Sequential Logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state_q <= IDLE;
            current_ram_addr_q <= '0;
            prev_trigger_fetch_q <= 1'b0;
            prev_trigger_exit_q <= 1'b0;
            beat_cnt_q <= '0;
        end else begin
            state_q <= state_d;
            current_ram_addr_q <= current_ram_addr_d;
            prev_trigger_fetch_q <= trigger_fetch;
            prev_trigger_exit_q <= trigger_boot_exit;
            beat_cnt_q <= beat_cnt_d;
        end
    end

endmodule