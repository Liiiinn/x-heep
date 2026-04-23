## CW305 minimal bring-up pin map (clock/reset/uart/trigger only)
## References:
## - chipwhisperer/firmware/fpgas/aes/vivado/cw305.xdc
## - CW305-Arm-DesignStart/src/hardware/CW305_designstart.xdc

## Clock and reset
set_property -dict {PACKAGE_PIN N13 IOSTANDARD LVCMOS33} [get_ports {clk_i}];     # pll_clk1
set_property -dict {PACKAGE_PIN R1  IOSTANDARD LVCMOS33} [get_ports {rst_i}];     # reset_pin_n (active low)

## UART over 20-pin header
set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports {uart_rx_i}]; # uart_rxd / IO2
set_property -dict {PACKAGE_PIN P16 IOSTANDARD LVCMOS33} [get_ports {uart_tx_o}]; # uart_txd / IO1

## Optional trigger for measurements
set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports {gpio_io[0]}]; # trig_out / IO4
