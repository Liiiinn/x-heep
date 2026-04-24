## CW305 pll_clk1 input is configured to 40MHz.
## Use a single root clock definition on clk_i to avoid duplicate
## generated clock trees for the same source.
create_clock -name clk_i -period 25.00 -waveform {0 12.50} [get_ports {clk_i}]
create_clock -name jtag_clk_pin -period 100.00 -waveform {0 50} [get_ports {jtag_tck_i}]
create_clock -name spi_slave_clk_pin -period 20.00 -waveform {0 10} [get_ports {spi_slave_sck_io}]


## JTAG and SPI-slave clocks are asynchronous to the system clock.
set_clock_groups -asynchronous \
  -group [get_clocks {clk_i}] \
  -group [get_clocks {jtag_clk_pin spi_slave_clk_pin}]

## Allow reset pin to route through non-dedicated path on CW305 minimal bring-up.
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets rst_led_o_OBUF]

## CW305 first bring-up only constrains minimal IO.
## Allow bitstream generation with other top-level ports intentionally left unassigned.
set_property SEVERITY {Warning} [get_drc_checks UCIO-1]
set_property SEVERITY {Warning} [get_drc_checks NSTD-1]
