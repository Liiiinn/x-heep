## Keep initial clocking aligned with existing FPGA flow.
create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports {clk_i}]

### Reset constraints copied from existing FPGA targets
set_false_path -from x_heep_system_i/core_v_mini_mcu_i/debug_subsystem_i/dm_obi_top_i/i_dm_top/i_dm_csrs/dmcontrol_q_reg\[ndmreset\]/C
set_false_path -from x_heep_system_i/rstgen_i/i_rstgen_bypass/synch_regs_q_reg[3]/C

## CW305 first bring-up only constrains minimal IO.
## Allow bitstream generation with other top-level ports intentionally left unassigned.
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]
set_property SEVERITY {Warning} [get_drc_checks NSTD-1]
