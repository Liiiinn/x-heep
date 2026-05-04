## CW305 (XC7A100T-FTG256) pin assignments for x-heep development
##
## Sources / verification:
##   chipwhisperer/firmware/fpgas/aes/vivado/cw305.xdc
##   newaetech/CW305-Arm-DesignStart/src/hardware/CW305_designstart.xdc
##   CW305 schematic rev. 04 (newaetech/cw305-artix-target)
##
## JP3 (40-pin debug header, LVCMOS33, 3V3 VCC-IO bank):
##   Rows labelled on PCB silkscreen.  Pins used here match ARM DesignStart layout.
## 20-pin CW connector (IO1-IO14) and on-board peripherals also used.

## ---------------------------------------------------------------------------
## Clock and reset
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN N13 IOSTANDARD LVCMOS33} [get_ports {clk_i}];     # pll_clk1
set_property -dict {PACKAGE_PIN R1  IOSTANDARD LVCMOS33} [get_ports {rst_i}];     # SW4 pushbutton (active low)

## ---------------------------------------------------------------------------
## UART  (20-pin CW connector)
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports {uart_rx_i}]; # IO2
set_property -dict {PACKAGE_PIN P16 IOSTANDARD LVCMOS33} [get_ports {uart_tx_o}]; # IO1

## ---------------------------------------------------------------------------
## JTAG  (JP3 debug header — same pins as ARM DesignStart)
## Connect a Digilent HS2 or compatible FTDI-based JTAG probe here.
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN B15 IOSTANDARD LVCMOS33} [get_ports {jtag_tck_i}];   # JP3: swclk / TCK
set_property -dict {PACKAGE_PIN A13 IOSTANDARD LVCMOS33} [get_ports {jtag_tms_i}];   # JP3: swdio / TMS
set_property -dict {PACKAGE_PIN B12 IOSTANDARD LVCMOS33} [get_ports {jtag_tdi_i}];   # JP3: TDI
set_property -dict {PACKAGE_PIN C11 IOSTANDARD LVCMOS33} [get_ports {jtag_tdo_o}];   # JP3: SWOTDO / TDO
set_property -dict {PACKAGE_PIN C14 IOSTANDARD LVCMOS33} [get_ports {jtag_trst_ni}]; # JP3: nTRST

## TCK is a clock input; Artix-7 cannot route it on a GCLK path without a BUFG.
## Suppress the placement warning for the un-buffered JTAG clock net.
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets -quiet {jtag_tck_i_IBUF}]

## ---------------------------------------------------------------------------
## SPI slave  (JP3 — remaining user pins for debug/communication)
## Connect to a host SPI master (e.g. ChipWhisperer capture, Raspberry Pi, PC
## via USB-SPI adapter) for RISC-V memory-mapped debug through obi_spi_slave.
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN B16 IOSTANDARD LVCMOS33} [get_ports {spi_slave_sck_io}];  # JP3 user pin
set_property -dict {PACKAGE_PIN C12 IOSTANDARD LVCMOS33} [get_ports {spi_slave_cs_io}];   # JP3 user pin
set_property -dict {PACKAGE_PIN A14 IOSTANDARD LVCMOS33} [get_ports {spi_slave_mosi_io}]; # JP3 user pin
set_property -dict {PACKAGE_PIN A15 IOSTANDARD LVCMOS33} [get_ports {spi_slave_miso_io}]; # JP3 user pin

## ---------------------------------------------------------------------------
## Boot control (DIP switch S2 on CW305)
## SW[0] = execute_from_flash  SW[1] = boot_select
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN J16 IOSTANDARD LVCMOS33} [get_ports {boot_select_i}];
set_property -dict {PACKAGE_PIN K16 IOSTANDARD LVCMOS33} [get_ports {execute_from_flash_i}];

## ---------------------------------------------------------------------------
## Status LEDs (on-board LEDs D1/D2/D3)
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN T2 IOSTANDARD LVCMOS33 DRIVE 8} [get_ports {rst_led_o}];
set_property -dict {PACKAGE_PIN T3 IOSTANDARD LVCMOS33 DRIVE 8} [get_ports {clk_led_o}];
set_property -dict {PACKAGE_PIN T4 IOSTANDARD LVCMOS33 DRIVE 8} [get_ports {exit_valid_o}];

## ---------------------------------------------------------------------------
## Measurement trigger (20-pin CW connector)
## ---------------------------------------------------------------------------
set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports {gpio_io[0]}]; # IO4 / trig_out

## ---------------------------------------------------------------------------
## Configuration properties (required for CW305 bitstream generation)
## ---------------------------------------------------------------------------
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
