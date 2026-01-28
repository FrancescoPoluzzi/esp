// Copyright 2022 OpenHW Group
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1



`ifndef SYNTHESIS
// Task for loading 'mem' with SystemVerilog system task $readmemh()
export "DPI-C" task tb_readHEX;
export "DPI-C" task tb_loadHEX;
export "DPI-C" task tb_writetoSram0;
export "DPI-C" task tb_writetoSram1;
export "DPI-C" task tb_writetoSram2;
export "DPI-C" task tb_writetoSram3;
export "DPI-C" task tb_writetoSram4;
export "DPI-C" task tb_writetoSram5;
export "DPI-C" task tb_writetoSram6;
export "DPI-C" task tb_writetoSram7;
export "DPI-C" task tb_writetoSram8;
export "DPI-C" task tb_writetoSram9;
export "DPI-C" task tb_writetoSram10;
export "DPI-C" task tb_writetoSram11;
export "DPI-C" task tb_writetoSram12;
export "DPI-C" task tb_writetoSram13;
export "DPI-C" task tb_writetoSram14;
export "DPI-C" task tb_writetoSram15;
export "DPI-C" task tb_getMemSize;
export "DPI-C" task tb_set_exit_loop;
export "DPI-C" task load_flash_hex;

import core_v_mini_mcu_pkg::*;

task tb_getMemSize;
  output int mem_size;
  mem_size = core_v_mini_mcu_pkg::MEM_SIZE;
endtask

task tb_readHEX;
  input string file;
  output logic [7:0] stimuli[core_v_mini_mcu_pkg::MEM_SIZE];
  $readmemh(file, stimuli);
endtask

task tb_loadHEX;
  input string file;
  //whether to use debug to write to memories
  logic [7:0] stimuli[core_v_mini_mcu_pkg::MEM_SIZE];
  int i, stimuli_base, w_addr, NumBytes;
  logic [31:0] addr;

  tb_readHEX(file, stimuli);
  tb_getMemSize(NumBytes);

`ifndef VERILATOR
  for (i = 0; i < NumBytes; i = i + 4) begin

    @(posedge x_heep_system_i.core_v_mini_mcu_i.clk_i);
    addr = i;
    #1;
    // write to memory
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_req_o = 1'b1;
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_addr_o = addr;
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_we_o = 1'b1;
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_be_o = 4'b1111;
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_wdata_o = {
      stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]
    };

    while (!x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_gnt_i)
      @(posedge x_heep_system_i.core_v_mini_mcu_i.clk_i);

    #1;
    force x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_req_o = 1'b0;

    wait (x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_rvalid_i);

    #1;

  end

  release x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_req_o;
  release x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_addr_o;
  release x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_we_o;
  release x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_be_o;
  release x_heep_system_i.core_v_mini_mcu_i.debug_subsystem_i.dm_obi_top_i.master_wdata_o;

`else
  for (i = 0; i < 32768; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram0(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 32768; i < 65536; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram1(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 65536; i < 98304; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram2(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 98304; i < 131072; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram3(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 131072; i < 163840; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram4(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 163840; i < 196608; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram5(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 196608; i < 229376; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram6(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 229376; i < 262144; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram7(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 262144; i < 294912; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram8(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 294912; i < 327680; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram9(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 327680; i < 360448; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram10(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 360448; i < 393216; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram11(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 393216; i < 425984; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram12(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 425984; i < 458752; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram13(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 458752; i < 491520; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram14(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end
  for (i = 491520; i < 524288; i = i + 4) begin
    if (((i / 4) & 0) == 0) begin
      w_addr = ((i / 4) >> 0) % 8192;
      tb_writetoSram15(w_addr, stimuli[i+3], stimuli[i+2], stimuli[i+1], stimuli[i]);
    end
  end

`endif

endtask

task tb_writetoSram0;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram0_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram0_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram0_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram1;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram1_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram1_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram1_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram2;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram2_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram2_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram2_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram3;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram3_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram3_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram3_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram4;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram4_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram4_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram4_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram5;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram5_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram5_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram5_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram6;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram6_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram6_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram6_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram7;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram7_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram7_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram7_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram8;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram8_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram8_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram8_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram9;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram9_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram9_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram9_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram10;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram10_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram10_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram10_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram11;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram11_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram11_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram11_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram12;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram12_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram12_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram12_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram13;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram13_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram13_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram13_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram14;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram14_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram14_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram14_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask

task tb_writetoSram15;
  input int addr;
  input [7:0] val3;
  input [7:0] val2;
  input [7:0] val1;
  input [7:0] val0;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram15_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
  release x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram15_i.tc_ram_i.sram[addr];
`else
  x_heep_system_i.core_v_mini_mcu_i.memory_subsystem_i.ram15_i.tc_ram_i.sram[addr] = {
    val3, val2, val1, val0
  };
`endif
endtask


task tb_set_exit_loop;
`ifdef VCS
  force x_heep_system_i.core_v_mini_mcu_i.ao_peripheral_subsystem_i.soc_ctrl_i.testbench_set_exit_loop[0] = 1'b1;
  release x_heep_system_i.core_v_mini_mcu_i.ao_peripheral_subsystem_i.soc_ctrl_i.testbench_set_exit_loop[0];
`else
  x_heep_system_i.core_v_mini_mcu_i.ao_peripheral_subsystem_i.soc_ctrl_i.testbench_set_exit_loop[0] = 1'b1;
`endif
endtask
`endif

task load_flash_hex;
  input string firmware_file;
  int i;
  for (i = 0; i <= 16 * 1024 * 1024; i = i + 1)
    gen_USE_EXTERNAL_DEVICE_EXAMPLE.flash_boot_i.memory[i] = 8'h00;
  $readmemh(firmware_file, gen_USE_EXTERNAL_DEVICE_EXAMPLE.flash_boot_i.memory);
endtask
