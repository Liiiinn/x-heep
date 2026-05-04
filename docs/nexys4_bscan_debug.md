# Nexys4 BSCANE Debug Flow

This flow uses only the PC and the Nexys4/Nexys A7 USB cable. No HS2, external
USB-UART, PMOD wiring, or `usbipd-win` USB forwarding is required. Windows owns
the board USB devices; WSL only builds software and runs `riscv-none-elf-gdb`.

## 1. Build a BSCANE bitstream in WSL

Run from the x-heep repository root after activating the project environment:

```sh
./scripts/nexys_bscan_build_bitstream.sh
```

This builds the Nexys A7 100T bitstream with:

```sh
FUSESOC_FLAGS=--flag=use_bscane_xilinx
```

## 2. Program the bitstream from Windows

Use Vivado Hardware Manager to program the generated bitstream.

If OpenOCD cannot open the FTDI device afterwards, use Zadig only on the FTDI
JTAG interface and install WinUSB or libusbK. Leave the UART interface as the
USB serial port. On the current Windows setup, UART is expected to remain visible
as `COM11`; if `COM11` disappears after Zadig, the UART side was changed and
must be restored before UART verification.

Use this check before and after Zadig:

```powershell
cd D:\UNI2\FINAL
.\nexys_bscan_driver_check_windows.ps1
```

Do not use Vivado auto connect after changing the JTAG interface to WinUSB. If a
new bitstream must be programmed again, restore the Digilent/FTDI driver first
or program the bitstream before switching the driver for OpenOCD.

## 3. Start OpenOCD from Windows

From PowerShell:

```powershell
cd D:\UNI2\FINAL
.\nexys_bscan_openocd_windows.ps1
```

If your board exposes JTAG on FTDI channel 1 instead of channel 0:

```powershell
.\nexys_bscan_openocd_windows.ps1 -Channel 1
```

## 4. Load the ELF from WSL GDB

Run from the x-heep repository root after activating the project environment:

```sh
make app PROJECT=hello_world TARGET=nexys-a7-100t LINKER=on_chip
./scripts/nexys_gdb_connect_bridge.sh
```

The ELF stays in WSL. GDB uses the Windows stdio bridge to reach OpenOCD.
The default GDB port is `13333` because some Windows/WSL setups reserve the
classic OpenOCD port range around `3333`.

## 5. Watch UART from Windows

In another PowerShell:

```powershell
cd D:\UNI2\FINAL
.\nexys_uart_monitor_windows.ps1 -Port COM11 -Baud 9600
```

Expected output for `hello_world` is:

```text
hello world!
```
