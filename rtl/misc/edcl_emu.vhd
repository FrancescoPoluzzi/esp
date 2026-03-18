library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.esp_global.all;
use work.amba.all;
use work.stdlib.all;
use work.sld_devices.all;
use work.devices.all;
use work.gencomp.all;
use work.leon3.all;
use work.uart.all;
use work.misc.all;
use work.net.all;
library unisim;
use unisim.VCOMPONENTS.all;
-- pragma translate_off
use work.sim.all;
use std.textio.all;
use work.stdio.all;
-- pragma translate_on
use work.monitor_pkg.all;
use work.esp_csr_pkg.all;
use work.sldacc.all;
use work.nocpackage.all;
use work.tile.all;
use work.coretypes.all;
use work.grlib_config.all;
use work.socmap.all;
use work.tb_pkg.all;
--use work.ahb_mst_emu_pkg.all;
--use work.ahb_mst_burst_pkg.all;

entity edcl_ahbmst_emu is 
  generic(
    hindex         : integer := 0
   );
  port (
    clk    :  in  std_ulogic;
    reset  :  in  std_ulogic;
    ahbmo  :  out ahb_mst_out_type;
    ahbmi  :  in  ahb_mst_in_type;
    edcl_oen_ctrl : out std_logic
  );
end entity edcl_ahbmst_emu;

architecture rtl of edcl_ahbmst_emu is
  signal edcl_ahbmi : ahb_mst_in_type;
  signal edcl_ahbmo : ahb_mst_out_type;
  signal word       : std_logic_vector(AHBDW-1 downto 0);
  signal word_next  : std_logic_vector(AHBDW-1 downto 0);
  signal word_temp  : std_logic_vector(31 downto 0) := X"00000000";
  --signal addr     : std_logic_vector(31 downto 0) := X"00000000";
  signal addr       : std_logic_vector(31 downto 0);
  signal addr_next  : std_logic_vector(31 downto 0);
  signal start      : std_ulogic := '1';
  signal last       : std_ulogic := '0';
    
  constant hconfig : ahb_config_type := (
      0 => ahb_device_reg (VENDOR_SLD, GAISLER_EDCLMST, 0, 0, 0),
      others => zero32);
  
  function default_ddr_addr
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  X"80000000";
      when others => return  X"40000000";
    end case;
  end default_ddr_addr;

  function default_boot_addr
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  X"00010000";
      when others => return  X"00000000";
    end case;
  end default_boot_addr;
  
  function default_rst_addr
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  X"60000400";
      when others => return  X"80000400";
    end case;
  end default_rst_addr;
  
  function default_word
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  X"0000000000000000";
      when others => return  X"00000000";
    end case;
  end default_word;
  
  function default_reset
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  X"0000000000000001";
      when others => return  X"00000001";
    end case;
  end default_reset;

  function ahbrd (
    word :  std_logic_vector(31 downto 0))
    return std_logic_vector is
  begin
    case ARCH_BITS is
      when 64 => return  word & word;
      when others => return word;
    end case;
  end ahbrd;

begin
 
    test_cryoai : process 

    --file bootloader : text open read_mode is "../soft-build/ibex/prom.txt";
    --file program    : text open read_mode is "../soft-build/ibex/systest.txt";
    file bootloader : text open read_mode is "../soft-build/" & CPU_STR & "/prom.txt";
    file program    : text open read_mode is "../soft-build/" & CPU_STR & "/systest.txt";
    variable text_word, text_data : line;
    variable word_var             : std_logic_vector(31 downto 0);
    variable ok                   : boolean;
    variable credit_to_clear      : boolean;
    variable credit_to_set        : boolean;
    variable program_length       : integer;
    variable data, tmp            : integer;
    variable wd_next              : std_logic_vector(31 downto 0); --integer;
    variable ad_next              : std_logic_vector(31 downto 0); --integer;
    --variable addr_next            : std_logic_vector(31 downto 0) := X"00000000";
  constant BREAK_ADDR_VAL : std_logic_vector(31 downto 0) := X"80000140";
    
    begin
  
    ahbmo.hconfig <= hconfig;
    ahbmo.hirq    <= (others => '0');
    ahbmo.hindex <= hindex;
    ahbmo.hlock    <= '0';
    ahbmo.hprot    <= "0011";

    ahbmo.htrans <= "00";
    ahbmo.hsize <= "000";
    ahbmo.hwrite <= '0';
    ahbmo.hbusreq <= '0';
    edcl_oen_ctrl <= '1';
    --ahbmo <= edcl_ahbmo;
    --edcl_ahbmi <= ahbmi;

    wait for 10 ns;
    wait until reset = '1';
    wait for 2000 ns;
 
    ---------------------------------------------------------------------------
    -- send first 2 soft resets
    ---------------------------------------------------------------------------
    report "sending reset"  severity note;
    wait for 10000 ns;
   
    addr <= default_rst_addr; --X"60000400"; --esp_init address
    word <= default_reset;

    for i in 0 to 1  loop

    ahbmo.hbusreq <= '1';

    --TODO: check if granted access based on specific slave
    wait until rising_edge(clk) and ahbmi.hready = '1';
    ahbmo.haddr <= addr;
    ahbmo.hwrite <= '1';
    ahbmo.hsize <= "010";
    ahbmo.htrans <= "10";
    ahbmo.hburst <= "001"; 
    ahbmo.hwdata <= word;
    
    wait until rising_edge(clk);
    ahbmo.htrans <= "00";
    ahbmo.hburst <= "000"; 
    --ahbmo.hwdata <= word;
    
    wait for 500 ns;

    wait until rising_edge(clk) and ahbmi.hready = '1';
    ahbmo.haddr <= addr;
    ahbmo.hwrite <= '1';
    ahbmo.hsize <= "010";
    ahbmo.htrans <= "10";
    ahbmo.hburst <= "001"; 
    ahbmo.hwdata <= default_word;
    
    wait until rising_edge(clk);
    ahbmo.hwrite <= '0';
    ahbmo.hsize <= "000";
    ahbmo.htrans <= "00";
    ahbmo.hburst <= "000"; 
    --ahbmo.hwdata <= word; 
    
    wait for 8000 ns;
    end loop;

    report "sent reset"  severity note;
    
    ---------------------------------------------------------------------------
    -- send bootloader
    ---------------------------------------------------------------------------

     --send bootloader binary (ahb burst)
    report "loading bootloader"  severity note;

    addr <= default_boot_addr; --X"00010000";  --bootrom for leon3
    word <= default_word;
	word_temp <= X"00000000";

    readline(bootloader, text_word);
    hread(text_word, word_var, ok);
    word <= ahbrd(word_var);

    ahbmo.hbusreq <= '1';

    wait until rising_edge(clk) and ahbmi.hready = '1';
    ahbmo.haddr <= addr;
    ahbmo.hwrite <= '1';
    ahbmo.hsize <= "010";
    ahbmo.htrans <= "10";
    ahbmo.hburst <= "001";
    ahbmo.hwdata <= word;

    ad_next := std_logic_vector(unsigned (addr + 4));

    wait until rising_edge(clk);
    ahbmo.haddr <= ad_next;
    ahbmo.htrans <= "11";
	
	ad_next := std_logic_vector(unsigned (ad_next + 4));
    -- send data
    while not endfile(bootloader) loop
      readline(bootloader, text_word);
      hread(text_word, word_var, ok);
      addr <= ad_next;
	  ad_next := std_logic_vector(unsigned (ad_next + 4));
	  word <= ahbrd(word_var);
      wait until rising_edge(clk) and ahbmi.hready = '1';
      ahbmo.htrans <= "11";
      ahbmo.hwdata <= word;
      ahbmo.htrans <= "11";
      ahbmo.haddr <= addr;
    end loop;

      --wait until rising_edge(clk);
      ahbmo.htrans <= "00";
      ahbmo.hburst <= "000";

      report "loaded bootloader"  severity note;
    --wait for 5000 ns;
    wait for 100 ns;


    ---------------------------------------------------------------------------
    -- send program binary
    ---------------------------------------------------------------------------

   report "loading binary"  severity note;
   
   addr <= default_ddr_addr; --X"80000000";  --DRAM base_addr for leon3
   word <= default_word;
   
   readline(program, text_word);
   hread(text_word, word_var, ok);
   word <= ahbrd(word_var);
   
   ahbmo.hbusreq <= '1';
   
   wait until rising_edge(clk) and ahbmi.hready = '1';
   ahbmo.haddr <= addr;
   ahbmo.hwrite <= '1';
   ahbmo.hsize <= "010";
   ahbmo.htrans <= "10";
   ahbmo.hburst <= "001";
   ahbmo.hwdata <= word;
   
   ad_next := std_logic_vector(unsigned (addr + 4));
   
   wait until rising_edge(clk);
   ahbmo.haddr <= ad_next;
   ahbmo.htrans <= "11";
   
   ad_next := std_logic_vector(unsigned (ad_next + 4));
   
   -- send data
   while not endfile(program) loop
     readline(program, text_word);
     hread(text_word, word_var, ok);
     addr <= ad_next;
     ad_next := std_logic_vector(unsigned (ad_next + 4));
     word <= ahbrd(word_var);
     wait until rising_edge(clk) and ahbmi.hready = '1';
     ahbmo.htrans <= "11";
     ahbmo.hwdata <= word;
     ahbmo.haddr <= addr;
   end loop;
   
   --  wait until rising_edge(clk);
     ahbmo.htrans <= "00";
     ahbmo.hburst <= "000";
   
     report "loaded program"  severity note;
   wait for 500 ns;

    ---------------------------------------------------------------------------
    -- send  2 soft resets
    ---------------------------------------------------------------------------
    report "sending reset"  severity note;
    --wait for 1000 ns;
   
    addr <= default_rst_addr; --X"60000400";  --esp_init
    word <= default_reset;

    ahbmo.hbusreq <= '1';
   
    for i in 0 to 1  loop
    --TODO: check if granted access based on specific slave
    wait until rising_edge(clk) and ahbmi.hready = '1';
    ahbmo.haddr <= addr;
    ahbmo.hwrite <= '1';
    ahbmo.hsize <= "010";
    ahbmo.htrans <= "10";
    ahbmo.hburst <= "001"; 
    ahbmo.hwdata <= word;
    
    wait until rising_edge(clk);
    ahbmo.htrans <= "00";
    ahbmo.hburst <= "000"; 
    --ahbmo.hwdata <= word;
    
    wait for 500 ns;

    wait until rising_edge(clk) and ahbmi.hready = '1';
    ahbmo.haddr <= addr;
    ahbmo.hwrite <= '1';
    ahbmo.hsize <= "010";
    ahbmo.htrans <= "10";
    ahbmo.hburst <= "001"; 
    ahbmo.hwdata <= default_word;
    
    wait until rising_edge(clk);
    ahbmo.hwrite <= '0';
    ahbmo.hsize <= "000";
    ahbmo.htrans <= "00";
    ahbmo.hburst <= "000"; 
    --ahbmo.hwdata <= word;
    --wait for 8000 ns;
    end loop;
    
    ahbmo.hbusreq <= '0'; 
    wait for 800 ns;

    report "sent reset"  severity note;
    

   end process;
end architecture rtl;

