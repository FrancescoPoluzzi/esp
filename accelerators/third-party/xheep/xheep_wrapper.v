// xheep_wrapper.v
`include "cf_math_pkg_xheep.sv"
`include "obi_pkg.sv"
`include "obi_pkg_ip.sv"
`include "esp_apb_pkg.sv"

module XHEEP_wrapper
#(
  parameter AXI_ADDR_WIDTH = 32, // match SoC (32 or 64 per CPU tile)
  parameter AXI_DATA_WIDTH = 32, // 64 for Ariane, 32 for Ibex
  parameter AXI_ID_WIDTH   = 4
)(
  // === Clocks / resets ===
  input  wire                     x_heep_clk,
  input  wire                     x_heep_rstn, // active-low
  input  wire                     direct_reset,   // active-low

  // ============ APB SLAVE (config/MMIO) ============
  input  wire [31:0]              paddr,
  input  wire                     psel,
  input  wire                     penable,
  input  wire                     pwrite,
  input  wire [31:0]              pwdata,
  output wire [31:0]              prdata,
  output wire                     pready,
  output wire                     pslverr,

  // ============ AXI4 MASTER ============
  output wire                     x_heep_axi_awvalid,
  input  wire                     x_heep_axi_awready,
  output wire [AXI_ID_WIDTH-1:0]  x_heep_axi_awid,
  output wire [7:0]               x_heep_axi_awlen,
  output wire [AXI_ADDR_WIDTH-1:0]x_heep_axi_awaddr,

  output wire                     x_heep_axi_wvalid,
  input  wire                     x_heep_axi_wready,
  output wire [AXI_DATA_WIDTH-1:0]x_heep_axi_wdata,
  output wire [(AXI_DATA_WIDTH/8)-1:0] x_heep_axi_wstrb,
  output wire                     x_heep_axi_wlast,

  output wire                     x_heep_axi_arvalid,
  input  wire                     x_heep_axi_arready,
  output wire [AXI_ID_WIDTH-1:0]  x_heep_axi_arid,
  output wire [7:0]               x_heep_axi_arlen,
  output wire [AXI_ADDR_WIDTH-1:0]x_heep_axi_araddr,

  input  wire [1:0]               x_heep_axi_bresp,
  input  wire                     x_heep_axi_bvalid,
  output wire                     x_heep_axi_bready,
  input  wire [AXI_ID_WIDTH-1:0]  x_heep_axi_bid,

  input  wire                     x_heep_axi_rvalid,
  output wire                     x_heep_axi_rready,
  input  wire [AXI_ID_WIDTH-1:0]  x_heep_axi_rid,
  input  wire                     x_heep_axi_rlast,
  input  wire [AXI_DATA_WIDTH-1:0]x_heep_axi_rdata,
  input  wire [1:0]               x_heep_axi_rresp,

  // === AXI sideband ===
  output wire [2:0]               x_heep_axi_awsize,
  output wire [2:0]               x_heep_axi_arsize,
  output wire [1:0]               x_heep_axi_awburst,
  output wire [1:0]               x_heep_axi_arburst,
  output wire                     x_heep_axi_awlock,
  output wire                     x_heep_axi_arlock,
  output wire [3:0]               x_heep_axi_awcache,
  output wire [3:0]               x_heep_axi_arcache,
  output wire [2:0]               x_heep_axi_awprot,
  output wire [2:0]               x_heep_axi_arprot,
  output wire [3:0]               x_heep_axi_awqos,
  output wire [3:0]               x_heep_axi_arqos,
  output wire [3:0]               x_heep_axi_awregion,
  output wire [3:0]               x_heep_axi_arregion,
  output wire [5:0]               x_heep_axi_awatop, // present in NVDLA; safe to tie off

  // ============ IRQ up to ESP ============
  output wire                     x_heep_intr
);

  // Tie-off AXI master for now (no DMA until you need it)
  assign x_heep_axi_awvalid = 1'b0;
  assign x_heep_axi_awid    = '0;
  assign x_heep_axi_awlen   = 8'd0;
  assign x_heep_axi_awaddr  = '0;

  assign x_heep_axi_wvalid  = 1'b0;
  assign x_heep_axi_wdata   = '0;
  assign x_heep_axi_wstrb   = '0;
  assign x_heep_axi_wlast   = 1'b0;

  assign x_heep_axi_arvalid = 1'b0;
  assign x_heep_axi_arid    = '0;
  assign x_heep_axi_arlen   = 8'd0;
  assign x_heep_axi_araddr  = '0;

  assign x_heep_axi_bready  = 1'b1;
  assign x_heep_axi_rready  = 1'b1;

  // AXI sideband defaults like NVDLA
  assign x_heep_axi_awsize   = $clog2(AXI_DATA_WIDTH/8);
  assign x_heep_axi_arsize   = $clog2(AXI_DATA_WIDTH/8);
  assign x_heep_axi_awburst  = 2'b01;
  assign x_heep_axi_arburst  = 2'b01;
  assign x_heep_axi_awlock   = 1'b0;
  assign x_heep_axi_arlock   = 1'b0;
  assign x_heep_axi_awcache  = 4'b0011;
  assign x_heep_axi_arcache  = 4'b0011;
  assign x_heep_axi_awprot   = 3'b010;
  assign x_heep_axi_arprot   = 3'b010;
  assign x_heep_axi_awqos    = 4'b0000;
  assign x_heep_axi_arqos    = 4'b0000;
  assign x_heep_axi_awregion = 4'b0000;
  assign x_heep_axi_arregion = 4'b0000;
  assign x_heep_axi_awatop   = 6'b000000;

  // Active-high reset for X-HEEP/bridge
  wire rst_ni = x_heep_rstn & direct_reset;
  wire clk_i  = x_heep_clk;  // single domain for bridge + X-HEEP

  import esp_apb_pkg::*;
  esp_apb_pkg::apb_req_t apb_req;
  esp_apb_pkg::apb_rsp_t apb_rsp;

  assign apb_req.paddr   = paddr;
  assign apb_req.psel    = psel;
  assign apb_req.penable = penable;
  assign apb_req.pwrite  = pwrite;
  assign apb_req.pprot   = 3'b010;   // non-privileged, data, secure
  assign apb_req.pwdata  = pwdata;
  assign apb_req.pstrb   = 4'hF;     // full-word writes

  assign prdata  = apb_rsp.prdata;
  assign pready  = apb_rsp.pready;
  assign pslverr = apb_rsp.pslverr;

  // Flatten struct fields for VCD dumping (QuestaSim doesn't support struct.field syntax)
  logic [31:0] dbg_apb_req_paddr;
  logic        dbg_apb_req_psel;
  logic        dbg_apb_req_penable;
  logic        dbg_apb_req_pwrite;
  logic [31:0] dbg_apb_req_pwdata;
  logic [3:0]  dbg_apb_req_pstrb;
  logic [2:0]  dbg_apb_req_pprot;
  
  logic [31:0] dbg_apb_rsp_prdata;
  logic        dbg_apb_rsp_pready;
  logic        dbg_apb_rsp_pslverr;

  always_comb begin
    dbg_apb_req_paddr   = apb_req.paddr;
    dbg_apb_req_psel    = apb_req.psel;
    dbg_apb_req_penable = apb_req.penable;
    dbg_apb_req_pwrite  = apb_req.pwrite;
    dbg_apb_req_pwdata  = apb_req.pwdata;
    dbg_apb_req_pstrb   = apb_req.pstrb;
    dbg_apb_req_pprot   = apb_req.pprot;
    
    dbg_apb_rsp_prdata  = apb_rsp.prdata;
    dbg_apb_rsp_pready  = apb_rsp.pready;
    dbg_apb_rsp_pslverr = apb_rsp.pslverr;
  end


/*  // ========================================================================
  // DEBUG: Monitor APB writes to X-HEEP
  // ========================================================================
  always @(posedge clk_i) begin
    if (apb_req.psel && apb_req.penable && apb_req.pwrite) begin
      $display("[XHEEP_WRAPPER APB] x_heep_rstn=%d direct_reset=%d  WRITE addr=0x%08x data=0x%08x", 
               x_heep_rstn, direct_reset, apb_req.paddr, apb_req.pwdata);
    end
    if (apb_req.psel && apb_req.penable && !apb_req.pwrite && apb_rsp.pready) begin
      $display("[XHEEP_WRAPPER APB] x_heep_rstn=%d direct_reset=%d READ  addr=0x%08x data=0x%08x", 
               x_heep_rstn, direct_reset, apb_req.paddr, apb_rsp.prdata);
    end
  end*/

  import obi_pkg::*;
  obi_pkg::obi_req_t  [0:0] esp_obi_m_req;
  obi_pkg::obi_resp_t  [0:0] esp_obi_m_rsp;

  // -------- APB -> OBI bridge --------
  apb_to_obi #(
    .ObiCfg   (obi_pkg::ObiDefaultConfig),
    .apb_req_t(esp_apb_pkg::apb_req_t),
    .apb_rsp_t(esp_apb_pkg::apb_rsp_t),
    .obi_req_t(obi_pkg::obi_req_t),
    .obi_rsp_t(obi_pkg::obi_resp_t)
  ) u_apb2obi (
    .clk_i     (clk_i),
    .rst_ni    (rst_ni),
    .apb_req_i (apb_req),
    .apb_rsp_o (apb_rsp),
    .obi_req_o (esp_obi_m_req[0]),
    .obi_rsp_i(esp_obi_m_rsp[0])
  );

  logic [31:0] dbg_obi_m_req_addr;
  logic        dbg_obi_m_req_req;
  logic [31:0] dbg_obi_m_req_wdata;
  logic        dbg_obi_m_req_we;
  logic [3:0]  dbg_obi_m_req_be;
  logic [31:0] dbg_obi_m_resp_rdata;
  logic        dbg_obi_m_resp_rvalid;
  logic        dbg_apb_m_resp_gnt;
  always_comb begin
    dbg_obi_m_req_req = esp_obi_m_req[0].req;
    dbg_obi_m_req_addr = esp_obi_m_req[0].addr;
    dbg_obi_m_req_wdata = esp_obi_m_req[0].wdata;
    dbg_obi_m_req_we = esp_obi_m_req[0].we;
    dbg_obi_m_req_be = esp_obi_m_req[0].be;
    dbg_obi_m_resp_rdata = esp_obi_m_rsp[0].rdata;
    dbg_obi_m_resp_rvalid = esp_obi_m_rsp[0].rvalid;
    dbg_apb_m_resp_gnt = esp_obi_m_rsp[0].gnt;
  end

// -------- X-HEEP top --------

  // Tie-off interface for XIF (X_EXT==0, but ports still require an actual interface)
  if_xif xif_if();

  // Sink for unused outputs to avoid floating ports
  logic unused_jtag_tdo;
  logic unused_uart_tx;
  logic unused_exit_valid;
  logic unused_dma_done;

  logic unused_ext_core_instr_req;
  logic unused_ext_core_data_req;
  logic unused_ext_debug_master_req;
  logic unused_ext_dma_read_req;
  logic unused_ext_dma_write_req;
  logic unused_ext_dma_addr_req;
  logic unused_ext_peripheral_slave_req;
  logic unused_ext_debug_req;

  core_v_mini_mcu #(
    .EXT_XBAR_NMASTER (1)
  ) u_xheep (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    .ext_xbar_master_req_i  (esp_obi_m_req),
    .ext_xbar_master_resp_o (esp_obi_m_rsp),

    // External master requests (we’re not using them here)
    .ext_core_instr_req_o         (unused_ext_core_instr_req),
    .ext_core_data_req_o          (unused_ext_core_data_req),
    .ext_debug_master_req_o       (unused_ext_debug_master_req),
    .ext_dma_read_req_o           (unused_ext_dma_read_req),
    .ext_dma_write_req_o          (unused_ext_dma_write_req),
    .ext_dma_addr_req_o           (unused_ext_dma_addr_req),
    .ext_peripheral_slave_req_o   (unused_ext_peripheral_slave_req),
    .ext_debug_req_o              (unused_ext_debug_req),

    // JTAG / UART / exit
    .jtag_tdo_o                   (unused_jtag_tdo),
    .uart_tx_o                    (unused_uart_tx),
    .exit_valid_o                 (unused_exit_valid),

    // DMA ports (unused in this ESP integration)
    .ext_dma_slot_tx_i            (1'b0),
    .ext_dma_slot_rx_i            (1'b0),
    .dma_done_o                   (unused_dma_done),

    .boot_select_i                        (1'b0),
    .execute_from_flash_i                 (1'b0),
    .jtag_tck_i                           (1'b0),
    .jtag_tms_i                           (1'b1),
    .jtag_trst_ni                         (rst_ni),
    .jtag_tdi_i                           (1'b0),
    .uart_rx_i                            (1'b1),
    .intr_vector_ext_i                    ('0),
    .intr_ext_peripheral_i                (1'b0),
    .cpu_subsystem_powergate_switch_ack_ni        (1'b1),
    .peripheral_subsystem_powergate_switch_ack_ni (1'b1),
    .external_subsystem_powergate_switch_ack_ni   (1'b1),
  
    .xif_compressed_if (xif_if),
    .xif_issue_if      (xif_if),
    .xif_commit_if     (xif_if),
    .xif_mem_if        (xif_if),
    .xif_mem_result_if (xif_if),
    .xif_result_if     (xif_if)

  );

  // --- Wave dump: flattened signals (QuestaSim-compatible) ---
  initial begin
    //$dumpfile("xheep.vcd");
    // top-level clock/resets
    $dumpvars(0, x_heep_clk);
    $dumpvars(0, x_heep_rstn);
    $dumpvars(0, direct_reset);
    // internal derived clk/reset
    $dumpvars(0, clk_i);
    $dumpvars(0, rst_ni);

    // top-level APB
    $dumpvars(0, paddr);
    $dumpvars(0, psel);
    $dumpvars(0, penable);
    $dumpvars(0, pwrite);
    $dumpvars(0, pwdata);
    $dumpvars(0, prdata);
    $dumpvars(0, pready);
    $dumpvars(0, pslverr);

    // flattened APB request (from struct)
    $dumpvars(0, dbg_apb_req_paddr);
    $dumpvars(0, dbg_apb_req_psel);
    $dumpvars(0, dbg_apb_req_penable);
    $dumpvars(0, dbg_apb_req_pwrite);
    $dumpvars(0, dbg_apb_req_pwdata);
    $dumpvars(0, dbg_apb_req_pstrb);
    $dumpvars(0, dbg_apb_req_pprot);

    // flattened APB response (from struct)
    $dumpvars(0, dbg_apb_rsp_prdata);
    $dumpvars(0, dbg_apb_rsp_pready);
    $dumpvars(0, dbg_apb_rsp_pslverr);

    // flattened OBI master request (from struct)
    $dumpvars(0, dbg_obi_m_req_req);
    $dumpvars(0, dbg_obi_m_req_addr);
    $dumpvars(0, dbg_obi_m_req_wdata);
    $dumpvars(0, dbg_obi_m_req_we);
    $dumpvars(0, dbg_obi_m_req_be);
    // flattened OBI master response (from struct)
    $dumpvars(0, dbg_obi_m_resp_rdata);
    $dumpvars(0, dbg_obi_m_resp_rvalid);
    $dumpvars(0, dbg_apb_m_resp_gnt);

  end

endmodule
