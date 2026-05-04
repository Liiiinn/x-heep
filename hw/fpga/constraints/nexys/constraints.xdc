## Nexys-A7-100T: board crystal is 100MHz; MMCM divides to 40MHz core clock.
## The board input clock constraint uses 10ns (100MHz); the MMCM-generated 40MHz
## clock (clk_out1_xilinx_clk_wizard_*) is automatically derived and constrained
## by Vivado from the block design.
create_clock -name sys_clk_pin    -period 10.00 -waveform {0 5.00} [get_ports {clk_i}]
create_clock -name jtag_clk_pin   -period 100.00 -waveform {0 50.00} [get_ports {jtag_tck_i}]
create_clock -name spi_slave_clk_pin -period 20.00 -waveform {0 10.00} [get_ports {spi_slave_sck_io}]


## Async clock domain declarations.
## -include_generated_clocks ensures the MMCM output (clk_out1_xilinx_clk_wizard_*)
## is included in the sys_clk_pin group, preventing TIMING-6/7 critical warnings.
set_clock_groups -asynchronous \
  -group [get_clocks -include_generated_clocks {sys_clk_pin}] \
  -group [get_clocks {jtag_clk_pin spi_slave_clk_pin}]

## spi_slave_clk_pin is also asynchronous to the MMCM output.
set_clock_groups -asynchronous \
  -group [get_clocks {spi_slave_clk_pin}] \
  -group [get_clocks -include_generated_clocks {sys_clk_pin}]


## -----------------------------------------------------------------------
## Reset false paths (from original nexys constraints.xdc)
## -----------------------------------------------------------------------
set_false_path -from x_heep_system_i/core_v_mini_mcu_i/debug_subsystem_i/dm_obi_top_i/i_dm_top/i_dm_csrs/dmcontrol_q_reg\[ndmreset\]/C
set_false_path -from x_heep_system_i/rstgen_i/i_rstgen_bypass/synch_regs_q_reg[3]/C


## -----------------------------------------------------------------------
## TIMING-18: set_false_path for pins with no real I/O timing requirement.
## (SPI slave CDC paths are already handled in constraints/common/spi_slave.xdc)
## -----------------------------------------------------------------------

## Quasi-static control inputs (change only at power-on).
set_false_path -from [get_ports {boot_select_i}]
set_false_path -from [get_ports {execute_from_flash_i}]
set_false_path -from [get_ports {rst_i}]

## GPIO (peripheral may be disabled or not time-critical at board level).
set_false_path -from [get_ports -quiet {gpio_io[*]}]
set_false_path -to   [get_ports -quiet {gpio_io[*]}]

## JTAG data pins (timing governed by JTAG clock domain, declared async above).
set_false_path -from [get_ports {jtag_tdi_i jtag_tms_i jtag_trst_ni}]
set_false_path -to   [get_ports -quiet {jtag_tdo_o}]

## UART (async serial, no sys_clk_pin-relative timing requirement).
set_false_path -from [get_ports -quiet {uart_rx_i}]
set_false_path -to   [get_ports -quiet {uart_tx_o}]

## SPI flash controller pins.
set_false_path -from [get_ports -quiet {spi_flash_sd_io[*]}]
set_false_path -to   [get_ports -quiet {spi_flash_sd_io[*] spi_flash_csb_o spi_flash_sck_o}]

## Status/LED outputs (no timing target external to device).
set_false_path -to   [get_ports -quiet {exit_valid_o exit_value_o clk_led_o pdm2pcm_clk_io rst_led_o}]


## -----------------------------------------------------------------------
## TIMING-14 fix: SPI slave SCK pad-mux constant propagation
## -----------------------------------------------------------------------
## The pad-mux register for spi_slave_sck selects between SPI-slave SCK (=0)
## and GPIO_14 (=1).  In our board bring-up configuration the mux is always 0.
## Declaring it as a static 0 lets Vivado constant-fold the LUT chains on the
## IOBUF T-pin path, removing them from the SPI clock tree (TIMING-14 #2/#3).
set_case_analysis 0 [get_pins -quiet \
    -filter {REF_PIN_NAME == Q} \
    -of_objects [get_cells -quiet -hierarchical \
        -filter {NAME =~ *u_pad_mux_spi_slave_sck*q_reg*}]]

## -----------------------------------------------------------------------
## DRC severity overrides
## -----------------------------------------------------------------------
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]
set_property SEVERITY {Warning} [get_drc_checks NSTD-1]
