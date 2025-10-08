// xheep_wrapper.v
`include "obi_pkg.sv"
`include "esp_apb_pkg.sv"

module XHEEP_wrapper
#(
  parameter AXI_ADDR_WIDTH = 32, // match SoC (32 or 64 per CPU tile)
  parameter AXI_DATA_WIDTH = 64, // 64 for Ariane-based SoCs
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

  // AXI sideband defaults (like NVDLA)
  assign x_heep_axi_awsize  = $clog2(AXI_DATA_WIDTH/8);
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
  wire clk_i  = x_heep_clk;

  // -------- Pack APB pins into a struct the bridge expects --------
  typedef struct packed {
    logic [31:0] paddr;
    logic        psel;
    logic        penable;
    logic        pwrite;
    logic [2:0]  pprot;   // APB3 attributes
    logic [31:0] pwdata;
    logic [3:0]  pstrb;   // APB4 strobes
  } apb_req_t;

  typedef struct packed {
    logic [31:0] prdata;
    logic        pready;
    logic        pslverr;
  } apb_rsp_t;

  apb_req_t apb_req;
  apb_rsp_t apb_rsp;

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

  import obi_pkg::*; 
  obi_pkg::obi_req_t  [0:0] esp_obi_m_req;
  obi_pkg::obi_resp_t  [0:0] esp_obi_m_rsp;

  // -------- APB -> OBI bridge --------
  apb_to_obi u_apb2obi (
    .clk_i     (clk_i),
    .rst_ni    (rst_ni),
    .apb_req_i (apb_req),
    .apb_rsp_o (apb_rsp),
    .obi_req_o (esp_obi_m_req[0]),
    .obi_resp_i(esp_obi_m_rsp[0])
  );
  // -------- OBI (core data) -> AXI bridge --------
  // Pack minimal AXI types expected by obi_to_axi.sv
  typedef struct packed {
    struct packed {
      logic [AXI_ADDR_WIDTH-1:0] addr;
      logic [1:0]  burst;
      logic [2:0]  size;
      logic [3:0]  cache;
      logic [2:0]  prot;
      logic        lock;
      logic [5:0]  atop;
      logic [0:0]  user; // we set AxiUserWidth=1 and tie to 0
    } aw;
    struct packed {
      logic [AXI_DATA_WIDTH-1:0]        data;
      logic [(AXI_DATA_WIDTH/8)-1:0]    strb;
      logic                              last;
    } w;
    struct packed {
      logic [AXI_ADDR_WIDTH-1:0] addr;
      logic [1:0]  burst;
      logic [2:0]  size;
      logic [3:0]  cache;
      logic [2:0]  prot;
      logic        lock;
    } ar;
    logic aw_valid;
    logic w_valid;
    logic ar_valid;
    logic b_ready;
    logic r_ready;
  } axi_req_t;

  typedef struct packed {
    struct packed {
      logic [1:0]  resp;
      logic [0:0]  user;
    } b;
    struct packed {
      logic [AXI_DATA_WIDTH-1:0] data;
      logic [1:0]  resp;
      logic [0:0]  user;
    } r;
    logic aw_ready;
    logic w_ready;
    logic ar_ready;
    logic b_valid;
    logic r_valid;
  } axi_rsp_t;

  axi_req_t x_axi_req;
  axi_rsp_t x_axi_rsp;

  // Map obi_to_axi AXI structs to discrete top-level AXI pins
  // AW
  assign x_heep_axi_awvalid = x_axi_req.aw_valid;
  assign x_axi_rsp.aw_ready = x_heep_axi_awready;
  assign x_heep_axi_awaddr  = x_axi_req.aw.addr;
  // W
  assign x_heep_axi_wvalid  = x_axi_req.w_valid;
  assign x_axi_rsp.w_ready  = x_heep_axi_wready;
  assign x_heep_axi_wdata   = x_axi_req.w.data;
  assign x_heep_axi_wstrb   = x_axi_req.w.strb;
  assign x_heep_axi_wlast   = x_axi_req.w.last;
  // AR
  assign x_heep_axi_arvalid = x_axi_req.ar_valid;
  assign x_axi_rsp.ar_ready = x_heep_axi_arready;
  assign x_heep_axi_araddr  = x_axi_req.ar.addr;
  // B
  assign x_axi_req.b_ready  = x_heep_axi_bready;
  assign x_heep_axi_bvalid  = x_axi_rsp.b_valid;
  assign x_heep_axi_bresp   = x_axi_rsp.b.resp;
  // R
  assign x_axi_req.r_ready  = x_heep_axi_rready;
  assign x_heep_axi_rvalid  = x_axi_rsp.r_valid;
  assign x_heep_axi_rdata   = x_axi_rsp.r.data;
  assign x_heep_axi_rresp   = x_axi_rsp.r.resp;
  // Sideband defaults are assigned later in this file.

  // Local OBI wires for core data port
  obi_pkg::obi_req_t  core_data_req;
  obi_pkg::obi_resp_t core_data_rsp;

  // Bridge core data OBI master to AXI master
  obi_to_axi #(
    .ObiCfg       (obi_pkg::ObiDefaultConfig),
    .obi_req_t    (obi_pkg::obi_req_t),
    .obi_rsp_t    (obi_pkg::obi_resp_t),
    .axi_req_t    (axi_req_t),
    .axi_rsp_t    (axi_rsp_t),
    .AxiUserWidth (1)
  ) u_cdata_obi2axi (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),
    .obi_req_i(core_data_req),
    .obi_rsp_o(core_data_rsp),
    .user_i  (1'b0),
    .axi_req_o(x_axi_req),
    .axi_rsp_i(x_axi_rsp),
    .axi_rsp_channel_sel(),
    .axi_rsp_b_user_o(),
    .axi_rsp_r_user_o(),
    .obi_rsp_user_i('0)
  );

  // -------- X-HEEP top --------
  core_v_mini_mcu #(
    .EXT_XBAR_NMASTER (1)
  ) u_xheep (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    .ext_xbar_master_req_i  (esp_obi_m_req),
    .ext_xbar_master_resp_o (esp_obi_m_rsp),

    .ext_core_instr_resp_i     ('0),
    .ext_core_data_resp_i      (core_data_rsp),
    .ext_debug_master_resp_i   ('0),
    .ext_dma_read_resp_i       ('0),
    .ext_dma_write_resp_i      ('0),
    .ext_dma_addr_resp_i       ('0),

    .ext_core_instr_req_o   (),
    .ext_core_data_req_o    (core_data_req),
    .ext_debug_master_req_o (),
    .ext_dma_read_req_o     (),
    .ext_dma_write_req_o    (),
    .ext_dma_addr_req_o     (),

    .boot_select_i                        (1'b0),
    .execute_from_flash_i                 (1'b0),
    .jtag_tck_i                           (1'b0),
    .jtag_tms_i                           (1'b1),
    .jtag_trst_ni                         (rst_ni),
    .jtag_tdi_i                           (1'b0),
    .jtag_tdo_o                           (),
    .uart_rx_i                            (1'b1),
    .uart_tx_o                            (),
    .intr_vector_ext_i                    ('0),
    .intr_timer_i                         (1'b0),
    .intr_software_i                      (1'b0),
    .intr_external_i                      (1'b0),
    .intr_ext_peripheral_i                (1'b0),
    .cpu_subsystem_powergate_switch_ack_ni        (1'b1),
    .peripheral_subsystem_powergate_switch_ack_ni (1'b1),
    .external_subsystem_powergate_switch_ack_ni   (1'b1)
  );

endmodule
