# CW305 Bring-up Checklist (x-heep / RISC-V)

**Target**: NewAE CW305 — Artix-7 XC7A100T-FTG256  
**System clock**: 40 MHz (pll_clk1 → MMCM → core)  
**Software `REFERENCE_CLOCK_Hz`**: 40 000 000  

---

## 引脚分配速查

| 功能 | CW305 接口 | FPGA 引脚 |
|------|-----------|----------|
| 系统时钟 | pll_clk1 | N13 |
| 复位（低有效）| SW4 | R1 |
| UART TX | 20-pin IO1 | P16 |
| UART RX | 20-pin IO2 | R16 |
| 测量触发 | 20-pin IO4 | T14 |
| JTAG TCK | JP3 B15 | B15 |
| JTAG TMS | JP3 A13 | A13 |
| JTAG TDI | JP3 B12 | B12 |
| JTAG TDO | JP3 C11 | C11 |
| JTAG TRST_N | JP3 C14 | C14 |
| SPI-slave SCK | JP3 B16 | B16 |
| SPI-slave CS | JP3 C12 | C12 |
| SPI-slave MOSI | JP3 A14 | A14 |
| SPI-slave MISO | JP3 A15 | A15 |
| boot_select | DIP S2[0] J16 | J16 |
| execute_from_flash | DIP S2[1] K16 | K16 |
| LED rst | D1 | T2 |
| LED clk | D2 | T3 |
| LED exit_valid | D3 | T4 |

---

## 阶段 1：生成比特流

```bash
cd ~/x-heep

# 生成 MCU RTL（若已运行可跳过）
make mcu-gen X_HEEP_CFG=configs/hqc_cv32e40px.hjson CPU=cv32e40px

# 编译 FPGA 设计（约 30–60 分钟）
make vivado-fpga FPGA_BOARD=cw305 \
  X_HEEP_CFG=configs/hqc_cv32e40px.hjson CPU=cv32e40px

# 比特流位置：
# build/.../cw305-vivado/openhwgroup.org_systems_core-v-mini-mcu_1.0.4.bit
```

---

## 阶段 2：烧写比特流

**方式 A（推荐）：ChipWhisperer Husky Python API**

通过 Husky 烧写比特流，同时自动初始化后续 UART 监控环境：

```bash
# 烧写 + 立即开始监控 printf 输出（一步完成）
python scripts/cw305_husky_monitor.py \
  --bsfile build/openhwgroup.org_systems_core-v-mini-mcu_1.0.4/cw305-vivado/openhwgroup.org_systems_core-v-mini-mcu_1.0.4.bit
```

或仅烧写（不进入监控循环）：

```python
import chipwhisperer as cw
scope = cw.scope()
scope.default_setup()
target = cw.target(scope, cw.targets.CW305,
                   bsfile='build/.../cw305-vivado/....bit',
                   force=True)
```

**方式 B（备用）：Vivado Hardware Manager**
```
Vivado → Open Hardware Manager → Open Target → Auto Connect
→ 右键 xc7a100t → Program Device
→ 选择: build/.../cw305-vivado/openhwgroup.org_systems_core-v-mini-mcu_1.0.4.bit
```

---

## 阶段 3：UART 监控

CW305 的 UART（IO1 = P16 / IO2 = R16）通过 **20-pin 连接器**引出，Husky 可通过内置 USART 直接读取，无需外接 USB-UART 转换器。

**方式 A（推荐）：Husky 直接监控**

```bash
# 确保 Husky 已通过 USB 接入 PC，20-pin 线缆连接至 CW305

# 仅监控（FPGA 已烧写）
python scripts/cw305_husky_monitor.py

# 烧写 + 监控（一步完成）
python scripts/cw305_husky_monitor.py \
  --bsfile build/.../cw305-vivado/openhwgroup.org_systems_core-v-mini-mcu_1.0.4.bit

# 同时写入日志
python scripts/cw305_husky_monitor.py --log hello_world_run1.log
```

脚本参数说明：

| 参数 | 说明 |
|------|------|
| `--bsfile FILE` | 烧写比特流后再监控（省略则跳过烧写） |
| `--baud N` | 波特率，默认 9600（与 `x-heep.h` 一致） |
| `--log FILE` | 同时将输出保存至日志文件 |

**方式 B（备用）：外接 USB-UART 转换器 + picocom**

如无 Husky，将 CP2102/CH340 连接到 CW305 IO1/IO2 引出点：

```bash
# 确认设备节点（拔插时观察）
dmesg --time-format iso | grep -E "tty|FTDI|cp210" | tail -5

# 打开串口（波特率 9600 与 x-heep.h 一致）
bash scripts/cw305_uart_capture.sh /dev/ttyUSB0 9600 run.log
# 或直接：picocom -b 9600 -r -l --imap lfcrlf /dev/ttyUSB0
```

---

## 阶段 4：JTAG 调试（固件加载）

### 接线

将 Digilent HS2 连接到 **JP3** (40-pin 扩展头)：

```
HS2 TCK  →  JP3 pin B15
HS2 TMS  →  JP3 pin A13
HS2 TDI  →  JP3 pin B12
HS2 TDO  →  JP3 pin C11
HS2 GND  →  JP3 GND
```

> JP3 引脚编号标注在 PCB 丝印上。

### 驱动安装

```bash
# 安装 FTDI 规则（仅首次）
echo 'ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6014", MODE="664", GROUP="plugdev"' \
  | sudo tee /etc/udev/rules.d/60-hs2.rules
sudo udevadm control --reload
sudo usermod -a -G plugdev $USER
# 重新拔插 HS2

# 验证
lsusb | grep "0403:6014"
```

### 启动 OpenOCD

```bash
cd ~/x-heep
openocd -f ./tb/core-v-mini-mcu-cw305-hs2.cfg

# 正常输出：
# Info : JTAG tap: riscv.cpu tap/device found: 0x10001c05
# Info : Examined RISC-V core; found 1 harts
# CW305 RISC-V core halted — ready for GDB on port 3333
```

---

## 阶段 5：编译并加载固件

### hello_world（功能验证）

```bash
cd ~/x-heep

# 编译（新终端）
make app PROJECT=hello_world TARGET=cw305 LINKER=on_chip

# GDB 加载（另一个新终端，OpenOCD 保持运行）
make -C sw gdb_connect
# 等效于：riscv32-unknown-elf-gdb sw/build/main.elf -x sw/gdbInit
# gdbInit 自动执行: load → continue
```

**UART 预期输出：**
```
Hello World!
```

### HQC 基准测试

```bash
make app PROJECT=HQC_phase2_lite_benchmark TARGET=cw305 LINKER=on_chip
make -C sw gdb_connect
# UART 观察 Keccak 周期数输出
```

---

## 阶段 6：SPI slave 通信验证

SPI slave 接线（JP3）：

```
主机 SCLK → JP3 B16 (spi_slave_sck)
主机 CS   → JP3 C12 (spi_slave_cs)
主机 MOSI → JP3 A14 (spi_slave_mosi)
主机 MISO → JP3 A15 (spi_slave_miso)
主机 GND  → JP3 GND
```

SPI slave 协议参考 x-heep obi_spi_slave 文档，可用 ChipWhisperer Python API 或树莓派 SPI 接口进行读写测试。

---

## 常见问题排查

| 现象 | 可能原因 | 排查 |
|------|---------|------|
| OpenOCD 无法连接 | HS2 未识别/接线错误 | `lsusb` 确认，检查接线 |
| tap/device found 但 ID 不符 | 比特流未烧写 | 重新烧写 |
| UART 无输出 | 固件未加载/波特率错误 | 确认 GDB load 成功，波特率 9600 |
| UART 乱码 | 时钟频率不一致 | 检查 x-heep.h REFERENCE_CLOCK_Hz = 40000000 |
| 复位不工作 | RST 低有效 | SW4 按下=复位，松开=运行 |
