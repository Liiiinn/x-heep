# Define design macros

set design_name      xilinx_clk_wizard
set in_clk_freq_MHz  40
set out_clk_freq_MHz 40


# Remove stale BD if present (e.g. from a previous run with different port names),
# so that create_bd_design always starts fresh and make_wrapper/add_files executes.
catch { close_bd_design $design_name }
catch { remove_files [get_files -quiet ${design_name}.bd] }

# Create block design
create_bd_design $design_name

# Create ports
set clk_40MHz [ create_bd_port -dir I -type clk -freq_hz [ expr $in_clk_freq_MHz * 1000000 ] clk_40MHz ]
set clk_out1_0 [ create_bd_port -dir O -type clk clk_out1_0 ]
set_property -dict [ list CONFIG.FREQ_HZ [ expr $out_clk_freq_MHz * 1000000 ] ] $clk_out1_0

# Create instance and set properties
# VCO = 40MHz * 20 / 1 = 800MHz (within Artix-7 range 600-1200MHz)
# clk_out1 = 800MHz / 20 = 40MHz
set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0 ]
set_property -dict [ list \
CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {40.000} \
CONFIG.MMCM_CLKFBOUT_MULT_F {20.000} \
CONFIG.MMCM_CLKOUT0_DIVIDE_F {20.000} \
CONFIG.MMCM_DIVCLK_DIVIDE {1} \
CONFIG.PRIM_IN_FREQ $in_clk_freq_MHz \
CONFIG.USE_LOCKED {false} \
CONFIG.USE_RESET {false} \
] $clk_wiz_0

# Create port connections
connect_bd_net -net clk_in1_0_1 [ get_bd_ports clk_40MHz ] [ get_bd_pins clk_wiz_0/clk_in1 ]
connect_bd_net -net clk_wiz_0_clk_out1 [ get_bd_ports clk_out1_0 ] [ get_bd_pins clk_wiz_0/clk_out1 ]

# Save and close block design
save_bd_design
close_bd_design $design_name

# create wrapper
set wrapper_path [ make_wrapper -fileset sources_1 -files [ get_files -norecurse xilinx_clk_wizard.bd ] -top ]
add_files -norecurse -fileset sources_1 $wrapper_path
