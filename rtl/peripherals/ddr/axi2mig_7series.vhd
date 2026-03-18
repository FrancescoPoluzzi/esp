-- Copyright (c) 2011-2026 Columbia University, System Level Design Group
-- SPDX-License-Identifier: Apache-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.all;
use work.amba.all;
use work.stdlib.all;
use work.devices.all;
use work.config_types.all;
use work.config.all;
library std;
use std.textio.all;

entity axi2mig_7series is
  generic(
	AXIDW					: integer := 64
    );
  port(
    ddr3_dq         : inout std_logic_vector(63 downto 0);
    ddr3_dqs_p      : inout std_logic_vector(7 downto 0);
    ddr3_dqs_n      : inout std_logic_vector(7 downto 0);
    ddr3_addr       : out   std_logic_vector(13 downto 0);
    ddr3_ba         : out   std_logic_vector(2 downto 0);
    ddr3_ras_n      : out   std_logic;
    ddr3_cas_n      : out   std_logic;
    ddr3_we_n       : out   std_logic;
    ddr3_reset_n    : out   std_logic;
    ddr3_ck_p       : out   std_logic_vector(0 downto 0);
    ddr3_ck_n       : out   std_logic_vector(0 downto 0);
    ddr3_cke        : out   std_logic_vector(0 downto 0);
    ddr3_cs_n       : out   std_logic_vector(0 downto 0);
    ddr3_dm         : out   std_logic_vector(7 downto 0);
    ddr3_odt        : out   std_logic_vector(0 downto 0);
	calib_done      : out   std_logic;
	rst_n_syn       : in    std_logic;
	rst_n_async     : in    std_logic;
    sys_clk_p       : in    std_logic;
    sys_clk_n       : in    std_logic;
    clk_ref_i       : in    std_logic;
    ui_clk          : out   std_logic;
    ui_clk_sync_rst : out   std_logic;
    ddr_axi_si      : in    axi_mosi_type;
    ddr_axi_so      : out   axi_somi_type    
    );
end;

architecture rtl of axi2mig_7series is

signal mmcm_locked : std_logic;


component mig is
   port (
    ddr3_dq              : inout std_logic_vector(63 downto 0);
    ddr3_addr            : out   std_logic_vector(13 downto 0);
    ddr3_ba              : out   std_logic_vector(2 downto 0);
    ddr3_ras_n           : out   std_logic;
    ddr3_cas_n           : out   std_logic;
    ddr3_we_n            : out   std_logic;
    ddr3_reset_n         : out   std_logic;
    ddr3_dqs_n           : inout std_logic_vector(7 downto 0);
    ddr3_dqs_p           : inout std_logic_vector(7 downto 0);
    ddr3_ck_p            : out   std_logic_vector(0 downto 0);
    ddr3_ck_n            : out   std_logic_vector(0 downto 0);
    ddr3_cke             : out   std_logic_vector(0 downto 0);
    ddr3_cs_n            : out   std_logic_vector(0 downto 0);
    ddr3_dm              : out   std_logic_vector(7 downto 0);
    ddr3_odt             : out   std_logic_vector(0 downto 0);
    sys_clk_p            : in    std_logic;
    sys_clk_n            : in    std_logic;
    clk_ref_i            : in    std_logic;
    -- Slave Interface Write Address Ports
    aresetn              : in std_logic;
    s_axi_awid           : in std_logic_vector(3 downto 0);
    s_axi_awaddr         : in std_logic_vector(29 downto 0);
    s_axi_awlen          : in std_logic_vector(7 downto 0);
    s_axi_awsize         : in std_logic_vector(2 downto 0);
    s_axi_awburst        : in std_logic_vector(1 downto 0);
    s_axi_awlock         : in std_logic;  
    s_axi_awcache        : in std_logic_vector(3 downto 0);
    s_axi_awprot         : in std_logic_vector(2 downto 0);
    s_axi_awqos          : in std_logic_vector(3 downto 0);
    s_axi_awvalid        : in    std_logic;
    s_axi_awready        : out   std_logic;
    --Slave Interface Write Data Ports
    s_axi_wdata          : in std_logic_vector(AXIDW-1 downto 0);
    s_axi_wstrb          : in std_logic_vector((AXIDW/8)-1 downto 0);
    s_axi_wlast          : in std_logic;
    s_axi_wvalid         : in std_logic;
    s_axi_wready         : out std_logic;
    -- Slave Interface Write Response Ports
    s_axi_bready         : in std_logic;
    s_axi_bid            : out std_logic_vector(3 downto 0);
    s_axi_bresp          : out std_logic_vector(1 downto 0);
    s_axi_bvalid         : out std_logic;
    -- Slave Interface Read Address Ports
    s_axi_arid           : in std_logic_vector(3 downto 0);
    s_axi_araddr         : in std_logic_vector(29 downto 0);
    s_axi_arlen          : in std_logic_vector(7 downto 0);
    s_axi_arsize         : in std_logic_vector(2 downto 0);
    s_axi_arburst        : in std_logic_vector(1 downto 0);
    s_axi_arlock         : in std_logic;  
    s_axi_arcache        : in std_logic_vector(3 downto 0);
    s_axi_arprot         : in std_logic_vector(2 downto 0);
    s_axi_arqos          : in std_logic_vector(3 downto 0);
    s_axi_arvalid        : in std_logic;
    s_axi_arready        : out std_logic;
    -- Slave Interface Read Data Ports
    s_axi_rready         : in std_logic;
    s_axi_rid            : out std_logic_vector(3 downto 0);
    s_axi_rdata          : out std_logic_vector(AXIDW-1 downto 0);
    s_axi_rresp          : out std_logic_vector(1 downto 0);
    s_axi_rlast          : out std_logic;
    s_axi_rvalid         : out std_logic;
    app_sr_req           : in    std_logic;
    app_ref_req          : in    std_logic;
    app_zq_req           : in    std_logic;
    app_sr_active        : out   std_logic;
    app_ref_ack          : out   std_logic;
    app_zq_ack           : out   std_logic;
    ui_clk               : out   std_logic;
    ui_clk_sync_rst      : out   std_logic;
    mmcm_locked          : out   std_logic;
    init_calib_complete  : out   std_logic;
    sys_rst              : in    std_logic
    );
 end component mig;
  
  begin
  
  MCB_inst : mig
    port map (
		ddr3_dq             => ddr3_dq,
		ddr3_dqs_p          => ddr3_dqs_p,
		ddr3_dqs_n          => ddr3_dqs_n,
		ddr3_addr           => ddr3_addr,
		ddr3_ba             => ddr3_ba,
		ddr3_ras_n          => ddr3_ras_n,
		ddr3_cas_n          => ddr3_cas_n,
		ddr3_we_n           => ddr3_we_n,
		ddr3_reset_n        => ddr3_reset_n,
		ddr3_ck_p           => ddr3_ck_p,
		ddr3_ck_n           => ddr3_ck_n,
		ddr3_cke            => ddr3_cke,
		ddr3_cs_n           => ddr3_cs_n,
		ddr3_dm             => ddr3_dm,
		ddr3_odt            => ddr3_odt,
		ui_clk              => ui_clk,
		ui_clk_sync_rst     => ui_clk_sync_rst,
		aresetn             => rst_n_syn,
		mmcm_locked         => mmcm_locked,
		app_sr_req          => '0',
		app_ref_req         => '0',
		app_zq_req          => '0',
		app_sr_active       => open,
		app_ref_ack         => open,
		app_zq_ack          => open,
		s_axi_awid          => ddr_axi_si.aw.id(3 downto 0),
		s_axi_awaddr        => ddr_axi_si.aw.addr(29 downto 0),
		s_axi_awlen         => ddr_axi_si.aw.len,
		s_axi_awsize        => ddr_axi_si.aw.size,
		s_axi_awburst       => ddr_axi_si.aw.burst,
		s_axi_awlock        => ddr_axi_si.aw.lock,
		s_axi_awcache       => ddr_axi_si.aw.cache,
		s_axi_awprot        => ddr_axi_si.aw.prot,
		s_axi_awqos         => ddr_axi_si.aw.qos,
		s_axi_awvalid       => ddr_axi_si.aw.valid,
		s_axi_awready       => ddr_axi_so.aw.ready,
		s_axi_wdata         => ddr_axi_si.w.data,
		s_axi_wstrb         => ddr_axi_si.w.strb,
		s_axi_wlast         => ddr_axi_si.w.last,
		s_axi_wvalid        => ddr_axi_si.w.valid,
		s_axi_wready        => ddr_axi_so.w.ready,
		s_axi_bid           => ddr_axi_so.b.id(3 downto 0),
		s_axi_bresp         => ddr_axi_so.b.resp,
		s_axi_bvalid        => ddr_axi_so.b.valid,
		s_axi_bready        => ddr_axi_si.b.ready,
		s_axi_arid          => ddr_axi_si.ar.id(3 downto 0),
		s_axi_araddr        => ddr_axi_si.ar.addr(29 downto 0),
		s_axi_arlen         => ddr_axi_si.ar.len,
		s_axi_arsize        => ddr_axi_si.ar.size,
		s_axi_arburst       => ddr_axi_si.ar.burst,
		s_axi_arlock        => ddr_axi_si.ar.lock,
		s_axi_arcache       => ddr_axi_si.ar.cache,
		s_axi_arprot        => ddr_axi_si.ar.prot,
		s_axi_arqos         => ddr_axi_si.ar.qos,
		s_axi_arvalid       => ddr_axi_si.ar.valid,
		s_axi_arready       => ddr_axi_so.ar.ready,
		s_axi_rid           => ddr_axi_so.r.id(3 downto 0),
		s_axi_rdata         => ddr_axi_so.r.data,
		s_axi_rresp         => ddr_axi_so.r.resp,
		s_axi_rlast         => ddr_axi_so.r.last,
		s_axi_rvalid        => ddr_axi_so.r.valid,
		s_axi_rready        => ddr_axi_si.r.ready,
		sys_clk_p           => sys_clk_p,
		sys_clk_n           => sys_clk_n,
		clk_ref_i           => clk_ref_i,
		init_calib_complete => calib_done,
		sys_rst             => rst_n_async
      );
end;

