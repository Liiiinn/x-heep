## CW305 pll_clk1 input is 40MHz; MMCM output is also 40MHz (period = 25ns).
## Use a single root clock definition on clk_i to avoid duplicate
## generated clock trees for the same source.
create_clock -name clk_i -period 25.00 -waveform {0 12.50} [get_ports {clk_i}]
create_clock -name jtag_clk_pin    -period 100.00 -waveform {0 50.00} [get_ports {jtag_tck_i}]
create_clock -name spi_slave_clk_pin -period 20.00 -waveform {0 10.00} [get_ports {spi_slave_sck_io}]


## Async clock domain declarations.
## Fix TIMING-6/7: use -include_generated_clocks so that clk_out1_xilinx_clk_wizard_*
## is included in the clk_i group; without it jtag_clk_pin↔clk_out1 paths remain timed.
set_clock_groups -asynchronous \
  -group [get_clocks -include_generated_clocks {clk_i}] \
  -group [get_clocks {jtag_clk_pin spi_slave_clk_pin}]

## spi_slave_clk_pin is also asynchronous to the MMCM output (generated from clk_i).
## Explicitly declaring this pair suppresses the inter-clock violations seen in the rpt.
set_clock_groups -asynchronous \
  -group [get_clocks {spi_slave_clk_pin}] \
  -group [get_clocks -include_generated_clocks {clk_i}]


## -----------------------------------------------------------------------
## TIMING-18: set_false_path for all pins that have no real I/O timing
## requirement in the CW305 bring-up context.
## -----------------------------------------------------------------------

## Quasi-static control inputs (change only at power-on, not per-cycle).
set_false_path -from [get_ports {boot_select_i}]
set_false_path -from [get_ports {execute_from_flash_i}]
set_false_path -from [get_ports {rst_i}]

## GPIO (peripheral is disabled; these pins are tie-offs in RTL).
set_false_path -from [get_ports -quiet {gpio_io[*]}]
set_false_path -to   [get_ports -quiet {gpio_io[*]}]

## JTAG data/control pins (timing governed by JTAG clock domain, already async above).
set_false_path -from [get_ports {jtag_tdi_i jtag_tms_i jtag_trst_ni}]
set_false_path -to   [get_ports -quiet {jtag_tdo_o}]

## UART (async serial, no clk_i-relative timing requirement).
set_false_path -from [get_ports -quiet {uart_rx_i}]
set_false_path -to   [get_ports -quiet {uart_tx_o}]

## SPI-slave data (already in async domain; suppress residual clk_i-relative warnings).
set_false_path -from [get_ports -quiet {spi_slave_cs_io spi_slave_mosi_io}]
set_false_path -to   [get_ports -quiet {spi_slave_miso_io}]

## SPI flash controller output pins.
set_false_path -from [get_ports -quiet {spi_flash_sd_io[*]}]
set_false_path -to   [get_ports -quiet {spi_flash_sd_io[*] spi_flash_csb_o spi_flash_sck_o}]

## Status/LED outputs and PDM clock (no timing target external to device).
set_false_path -to   [get_ports -quiet {exit_valid_o exit_value_o clk_led_o pdm2pcm_clk_io rst_led_o}]


## -----------------------------------------------------------------------
## TIMING-14 fix: SPI slave SCK pad-mux constant propagation
## -----------------------------------------------------------------------
## The pad-mux register for spi_slave_sck selects between SPI-slave SCK (=0)
## and GPIO_14 (=1).  In our board bring-up configuration the mux is always 0.
## Declaring it as a static 0 allows Vivado to constant-fold the two LUT chains
## that drive the IOBUF T-pin (the "~pad_oe_i" LUT + the OE-mux LUT), removing
## them from the SPI-slave clock tree and eliminating TIMING-14 violations #2/#3.
set_case_analysis 0 [get_pins -quiet \
    -filter {REF_PIN_NAME == Q} \
    -of_objects [get_cells -quiet -hierarchical \
        -filter {NAME =~ *u_pad_mux_spi_slave_sck*q_reg*}]]

## -----------------------------------------------------------------------
## Physical routing exceptions
## -----------------------------------------------------------------------

## Allow reset LED net to route through non-dedicated path on CW305 minimal bring-up.
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets -quiet {rst_led_o_OBUF}]


## -----------------------------------------------------------------------
## DRC severity overrides (bring-up: many pins are intentionally unassigned)
## -----------------------------------------------------------------------
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]
set_property SEVERITY {Warning} [get_drc_checks NSTD-1]
