// apb_to_obi.sv — Flat OBI (X-HEEP) + APB via packages (no parameter type ports)

`include "obi_pkg.sv"
`include "esp_apb_pkg.sv"

module apb_to_obi (
  input  logic                 clk_i,
  input  logic                 rst_ni,

  // APB subordinate port
  input  esp_apb_pkg::apb_req_t  apb_req_i,
  output esp_apb_pkg::apb_rsp_t  apb_rsp_o,

  // OBI manager port (X-HEEP flat interface types)
  output obi_pkg::obi_req_t      obi_req_o,
  input  obi_pkg::obi_resp_t     obi_resp_i
);

  import esp_apb_pkg::*;
  import obi_pkg::*;

  typedef enum logic [0:0] {ADDR, RESP} state_e;
  state_e state_q, state_d;

  // ---------------- Combinational ----------------
  always_comb begin
    // Safe defaults
    obi_req_o  = '0;
    apb_rsp_o  = '0;

    // Static OBI payload from APB fields
    obi_req_o.addr  = apb_req_i.paddr;
    obi_req_o.we    = apb_req_i.pwrite;
    obi_req_o.be    = apb_req_i.pwrite ? apb_req_i.pstrb : '1;
    obi_req_o.wdata = apb_req_i.pwdata;

    // APB read data from OBI read channel
    apb_rsp_o.prdata  = obi_resp_i.rdata;
    apb_rsp_o.pslverr = 1'b0;

    // FSM default
    state_d = state_q;

    unique case (state_q)
      ADDR: begin
        obi_req_o.req = apb_req_i.psel;
        if (obi_req_o.req && obi_resp_i.gnt) state_d = RESP;
      end

      RESP: begin
        if (obi_resp_i.rvalid) begin
          apb_rsp_o.pready = 1'b1;
          state_d = ADDR;
        end
      end

      default: state_d = ADDR;
    endcase
  end

  // ---------------- State FF ----------------
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) state_q <= ADDR;
    else         state_q <= state_d;
  end

endmodule
