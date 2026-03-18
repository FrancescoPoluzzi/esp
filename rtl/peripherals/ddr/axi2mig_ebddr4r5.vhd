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

entity axi2mig_ebddr4r5 is
  generic(
    AXIDW                   : integer := 64
  );
  port(
    c0_sys_clk_p     : in    std_logic;
    c0_sys_clk_n     : in    std_logic;
    c0_ddr4_act_n    : out   std_logic;
    c0_ddr4_adr      : out   std_logic_vector(16 downto 0);
    c0_ddr4_ba       : out   std_logic_vector(1 downto 0);
    c0_ddr4_bg       : out   std_logic_vector(1 downto 0);
    c0_ddr4_cke      : out   std_logic_vector(1 downto 0);
    c0_ddr4_odt      : out   std_logic_vector(1 downto 0);
    c0_ddr4_cs_n     : out   std_logic_vector(1 downto 0);
    c0_ddr4_ck_t     : out   std_logic_vector(0 downto 0);
    c0_ddr4_ck_c     : out   std_logic_vector(0 downto 0);
    c0_ddr4_reset_n  : out   std_logic;
    c0_ddr4_dm_dbi_n : inout std_logic_vector(8 downto 0);
    c0_ddr4_dq       : inout std_logic_vector(71 downto 0);
    c0_ddr4_dqs_c    : inout std_logic_vector(8 downto 0);
    c0_ddr4_dqs_t    : inout std_logic_vector(8 downto 0);
    ddr_axi_si       : in    axi_mosi_type;
    ddr_axi_so       : out   axi_somi_type;
    calib_done       : out   std_logic;
    rst_n_syn        : in    std_logic;
    rst_n_async      : in    std_logic;
    clk_amba         : out    std_logic;
    ui_clk           : out   std_logic;
    ui_clk_sync_rst  : out   std_logic);
end;

architecture rtl of axi2mig_ebddr4r5 is

COMPONENT mig
  port (
    c0_init_calib_complete : OUT STD_LOGIC;
    dbg_clk : OUT STD_LOGIC;
    c0_sys_clk_p : IN STD_LOGIC;
    c0_sys_clk_n : IN STD_LOGIC;
    dbg_bus : OUT STD_LOGIC_VECTOR(511 DOWNTO 0);
    c0_ddr4_adr : OUT STD_LOGIC_VECTOR(16 DOWNTO 0);
    c0_ddr4_ba : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_cke : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_cs_n : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_dm_dbi_n : INOUT STD_LOGIC_VECTOR(8 DOWNTO 0);
    c0_ddr4_dq : INOUT STD_LOGIC_VECTOR(71 DOWNTO 0);
    c0_ddr4_dqs_c : INOUT STD_LOGIC_VECTOR(8 DOWNTO 0);
    c0_ddr4_dqs_t : INOUT STD_LOGIC_VECTOR(8 DOWNTO 0);
    c0_ddr4_odt : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_bg : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_reset_n : OUT STD_LOGIC;
    c0_ddr4_act_n : OUT STD_LOGIC;
    c0_ddr4_ck_c : OUT STD_LOGIC_VECTOR(0 DOWNTO 0);
    c0_ddr4_ck_t : OUT STD_LOGIC_VECTOR(0 DOWNTO 0);
    c0_ddr4_ui_clk : OUT STD_LOGIC;
    c0_ddr4_ui_clk_sync_rst : OUT STD_LOGIC;
    c0_ddr4_aresetn : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_awvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_awready : OUT STD_LOGIC;
    c0_ddr4_s_axi_ctrl_awaddr : IN STD_LOGIC_VECTOR(31 DOWNTO 0);
    c0_ddr4_s_axi_ctrl_wvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_wready : OUT STD_LOGIC;
    c0_ddr4_s_axi_ctrl_wdata : IN STD_LOGIC_VECTOR(31 DOWNTO 0);
    c0_ddr4_s_axi_ctrl_bvalid : OUT STD_LOGIC;
    c0_ddr4_s_axi_ctrl_bready : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_bresp : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_s_axi_ctrl_arvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_arready : OUT STD_LOGIC;
    c0_ddr4_s_axi_ctrl_araddr : IN STD_LOGIC_VECTOR(31 DOWNTO 0);
    c0_ddr4_s_axi_ctrl_rvalid : OUT STD_LOGIC;
    c0_ddr4_s_axi_ctrl_rready : IN STD_LOGIC;
    c0_ddr4_s_axi_ctrl_rdata : OUT STD_LOGIC_VECTOR(31 DOWNTO 0);
    c0_ddr4_s_axi_ctrl_rresp : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_interrupt : OUT STD_LOGIC;
    c0_ddr4_s_axi_awid : IN STD_LOGIC_VECTOR(9 DOWNTO 0);
    c0_ddr4_s_axi_awaddr : IN STD_LOGIC_VECTOR(33 DOWNTO 0);
    c0_ddr4_s_axi_awlen : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
    c0_ddr4_s_axi_awsize : IN STD_LOGIC_VECTOR(2 DOWNTO 0);
    c0_ddr4_s_axi_awburst : IN STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_s_axi_awlock : IN STD_LOGIC_VECTOR(0 DOWNTO 0);
    c0_ddr4_s_axi_awcache : IN STD_LOGIC_VECTOR(3 DOWNTO 0);
    c0_ddr4_s_axi_awprot : IN STD_LOGIC_VECTOR(2 DOWNTO 0);
    c0_ddr4_s_axi_awqos : IN STD_LOGIC_VECTOR(3 DOWNTO 0);
    c0_ddr4_s_axi_awvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_awready : OUT STD_LOGIC;
    c0_ddr4_s_axi_wdata : in std_logic_vector(AXIDW-1 downto 0);
    c0_ddr4_s_axi_wstrb : in std_logic_vector((AXIDW/8)-1 downto 0);
    c0_ddr4_s_axi_wlast : IN STD_LOGIC;
    c0_ddr4_s_axi_wvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_wready : OUT STD_LOGIC;
    c0_ddr4_s_axi_bready : IN STD_LOGIC;
    c0_ddr4_s_axi_bid : OUT STD_LOGIC_VECTOR(9 DOWNTO 0);
    c0_ddr4_s_axi_bresp : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_s_axi_bvalid : OUT STD_LOGIC;
    c0_ddr4_s_axi_arid : IN STD_LOGIC_VECTOR(9 DOWNTO 0);
    c0_ddr4_s_axi_araddr : IN STD_LOGIC_VECTOR(33 DOWNTO 0);
    c0_ddr4_s_axi_arlen : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
    c0_ddr4_s_axi_arsize : IN STD_LOGIC_VECTOR(2 DOWNTO 0);
    c0_ddr4_s_axi_arburst : IN STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_s_axi_arlock : IN STD_LOGIC_VECTOR(0 DOWNTO 0);
    c0_ddr4_s_axi_arcache : IN STD_LOGIC_VECTOR(3 DOWNTO 0);
    c0_ddr4_s_axi_arprot : IN STD_LOGIC_VECTOR(2 DOWNTO 0);
    c0_ddr4_s_axi_arqos : IN STD_LOGIC_VECTOR(3 DOWNTO 0);
    c0_ddr4_s_axi_arvalid : IN STD_LOGIC;
    c0_ddr4_s_axi_arready : OUT STD_LOGIC;
    c0_ddr4_s_axi_rready : IN STD_LOGIC;
    c0_ddr4_s_axi_rlast : OUT STD_LOGIC;
    c0_ddr4_s_axi_rvalid : OUT STD_LOGIC;
    c0_ddr4_s_axi_rresp : OUT STD_LOGIC_VECTOR(1 DOWNTO 0);
    c0_ddr4_s_axi_rid : OUT STD_LOGIC_VECTOR(9 DOWNTO 0);
    c0_ddr4_s_axi_rdata : out std_logic_vector(AXIDW-1 downto 0);
    addn_ui_clkout1 : OUT STD_LOGIC;
    sys_rst : IN STD_LOGIC 
  );
end component;

  signal sys_rst : std_logic;
  signal c0_ddr4_s_axi_awlock  : std_logic_vector (0 downto 0);
  signal c0_ddr4_s_axi_arlock  : std_logic_vector (0 downto 0);
  signal c0_ddr4_s_axi_araddr  : std_logic_vector (33 downto 0);
  signal c0_ddr4_s_axi_awaddr  : std_logic_vector (33 downto 0);

  begin

  sys_rst <= not rst_n_async;
  c0_ddr4_s_axi_awlock(0) 	<= ddr_axi_si.aw.lock;
  c0_ddr4_s_axi_arlock(0) 	<= ddr_axi_si.ar.lock;
  c0_ddr4_s_axi_araddr      <= "00" & ddr_axi_si.ar.addr(31 downto 0);  
  c0_ddr4_s_axi_awaddr      <= "00" & ddr_axi_si.aw.addr(31 downto 0);

  --c0_ddr4_s_axi_araddr  	<= "00" & ddr_axi_si.ar.addr;
  --c0_ddr4_s_axi_awaddr  	<= "00" & ddr_axi_si.aw.addr; 

  mig_inst : mig 
    PORT MAP (
     c0_init_calib_complete      => calib_done,
     dbg_clk                     => open,
     c0_sys_clk_p                => c0_sys_clk_p,
     c0_sys_clk_n                => c0_sys_clk_n,
     dbg_bus                     => open,
     c0_ddr4_adr                 => c0_ddr4_adr,
     c0_ddr4_ba                  => c0_ddr4_ba,
     c0_ddr4_cke                 => c0_ddr4_cke,
     c0_ddr4_cs_n                => c0_ddr4_cs_n,
     c0_ddr4_dm_dbi_n            => c0_ddr4_dm_dbi_n,
     c0_ddr4_dq                  => c0_ddr4_dq,
     c0_ddr4_dqs_c               => c0_ddr4_dqs_c,
     c0_ddr4_dqs_t               => c0_ddr4_dqs_t,
     c0_ddr4_odt                 => c0_ddr4_odt,
     c0_ddr4_bg                  => c0_ddr4_bg,
     c0_ddr4_reset_n             => c0_ddr4_reset_n,
     c0_ddr4_act_n               => c0_ddr4_act_n,
     c0_ddr4_ck_c                => c0_ddr4_ck_c,
     c0_ddr4_ck_t                => c0_ddr4_ck_t,
     c0_ddr4_ui_clk              => ui_clk,
     c0_ddr4_ui_clk_sync_rst     => ui_clk_sync_rst,
     c0_ddr4_aresetn             => rst_n_syn,
     c0_ddr4_s_axi_awid          => ddr_axi_si.aw.id,
     c0_ddr4_s_axi_awaddr        => c0_ddr4_s_axi_awaddr,
     c0_ddr4_s_axi_awlen         => ddr_axi_si.aw.len,
     c0_ddr4_s_axi_awsize        => ddr_axi_si.aw.size,
     c0_ddr4_s_axi_awburst       => ddr_axi_si.aw.burst,
     c0_ddr4_s_axi_awlock        => c0_ddr4_s_axi_awlock,
     c0_ddr4_s_axi_awcache       => ddr_axi_si.aw.cache,
     c0_ddr4_s_axi_awprot        => ddr_axi_si.aw.prot,
     c0_ddr4_s_axi_awqos         => (others => '0'),
     c0_ddr4_s_axi_awvalid       => ddr_axi_si.aw.valid,
     c0_ddr4_s_axi_awready       => ddr_axi_so.aw.ready,
     c0_ddr4_s_axi_wdata         => ddr_axi_si.w.data,
     c0_ddr4_s_axi_wstrb         => ddr_axi_si.w.strb,
     c0_ddr4_s_axi_wlast         => ddr_axi_si.w.last,
     c0_ddr4_s_axi_wvalid        => ddr_axi_si.w.valid,
     c0_ddr4_s_axi_wready        => ddr_axi_so.w.ready,
     c0_ddr4_s_axi_bready        => ddr_axi_si.b.ready,
     c0_ddr4_s_axi_bid           => ddr_axi_so.b.id,
     c0_ddr4_s_axi_bresp         => ddr_axi_so.b.resp,
     c0_ddr4_s_axi_bvalid        => ddr_axi_so.b.valid,
     c0_ddr4_s_axi_arid          => ddr_axi_si.ar.id,
     c0_ddr4_s_axi_araddr        => c0_ddr4_s_axi_araddr,
     c0_ddr4_s_axi_arlen         => ddr_axi_si.ar.len,
     c0_ddr4_s_axi_arsize        => ddr_axi_si.ar.size,
     c0_ddr4_s_axi_arburst       => ddr_axi_si.ar.burst,
     c0_ddr4_s_axi_arlock        => c0_ddr4_s_axi_arlock,
     c0_ddr4_s_axi_arcache       => ddr_axi_si.ar.cache,
     c0_ddr4_s_axi_arprot        => ddr_axi_si.ar.prot,
     c0_ddr4_s_axi_arqos         => (others => '0'),
     c0_ddr4_s_axi_arvalid       => ddr_axi_si.ar.valid,
     c0_ddr4_s_axi_arready       => ddr_axi_so.ar.ready,
     c0_ddr4_s_axi_rready        => ddr_axi_si.r.ready,
     c0_ddr4_s_axi_rlast         => ddr_axi_so.r.last,
     c0_ddr4_s_axi_rvalid        => ddr_axi_so.r.valid,
     c0_ddr4_s_axi_rresp         => ddr_axi_so.r.resp,
     c0_ddr4_s_axi_rid           => ddr_axi_so.r.id,
     c0_ddr4_s_axi_rdata         => ddr_axi_so.r.data,
     c0_ddr4_interrupt			 => open,
     sys_rst                     => sys_rst,
     addn_ui_clkout1 			 => clk_amba,
	 c0_ddr4_s_axi_ctrl_awvalid  => '0',
	 c0_ddr4_s_axi_ctrl_awready	 => open,
	 c0_ddr4_s_axi_ctrl_awaddr 	 => (others =>'0'),
	 c0_ddr4_s_axi_ctrl_wvalid 	 => '0',
	 c0_ddr4_s_axi_ctrl_wready 	 => open,
	 c0_ddr4_s_axi_ctrl_wdata 	 => (others => '0'),
	 c0_ddr4_s_axi_ctrl_bvalid 	 => open,
	 c0_ddr4_s_axi_ctrl_bready 	 => '1',
	 c0_ddr4_s_axi_ctrl_bresp 	 => open,
	 c0_ddr4_s_axi_ctrl_arvalid  => '0',
	 c0_ddr4_s_axi_ctrl_arready	 => open,
	 c0_ddr4_s_axi_ctrl_araddr 	 => (others => '0'),
	 c0_ddr4_s_axi_ctrl_rvalid 	 => open,
	 c0_ddr4_s_axi_ctrl_rready 	 => '1',
	 c0_ddr4_s_axi_ctrl_rdata 	 => open,
	 c0_ddr4_s_axi_ctrl_rresp 	 => open
    );
end;
