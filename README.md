# MPTT — Mini Push To Talk

> LoRa 对讲机 v0.1 | STM32WLE5 + WM8960 | 2层 FR-4 35×55mm

基于 E77-400M22S (STM32WLE5) 和 WM8960 音频 Codec 的 LoRa 手持对讲机。单节锂电池供电，USB-C 充电，SMA 外接天线。

## 目录结构

```
mptt/
├── README.md                 本文件
├── docs/                     设计文档 (见 docs/README.md)
├── hardware/                 硬件设计 (原理图、BOM、PCB、EasyEDA 工程)
├── reference/datasheets/     芯片数据手册
└── firmware/                 固件
    ├── build.bat             构建主固件 (Windows)
    ├── Makefile              构建 / 烧录快捷入口
    ├── link.ld               链接脚本
    ├── startup.c             启动代码
    ├── include/              头文件
    │   ├── stm32wle5xx.h     MCU 寄存器定义
    │   └── audio_wm8960.h    音频驱动 API
    ├── src/                  源代码
    │   ├── main.c            应用层 (PTT 状态机 + DSP 后处理)
    │   └── audio_wm8960.c    音频底层 (MCLK/I2C/I2S/WM8960, 已冻结)
    ├── tools/                调试 / 验证脚本 (pyocd)
    │   ├── selftest.py       自动验证 (模拟 PTT, 无需人工)
    │   ├── readproc.py       读取处理后缓冲 → WAV
    │   ├── readdiag.py       读取 DIAG 诊断区
    │   └── ...
    └── build/                构建产物 (gitignore)
        ├── fw.elf
        └── fw.bin
```

## 固件快速开始

```bat
cd firmware
build.bat
pyocd flash -t stm32wle5cbux build\fw.bin
python tools\selftest.py
```

音频底层配置与调试知识库见 `D:\kb\wm8960\mptt-从零调试实战手册.md`。

## 核心器件

| 位号 | 型号 | 功能 |
|------|------|------|
| U1 | E77-400M22S | STM32WLE5 LoRa 模组, 400-470MHz |
| U2 | WM8960CGEFL/RV | 音频 Codec + 1W D类功放 |
| U3 | ME6211C33M5G-N | 3.3V 500mA LDO |
| U4 | TP4056-42-ESOP8 | 锂电池充电管理 |
| U5 | DW01A + U6 8205A | 电池保护 + MOSFET 开关 |

## 设计工具

- **EDA**: 立创EDA专业版
- **PCB**: JLCPCB 2层 FR-4, 35×55mm

## 许可证

MIT License
