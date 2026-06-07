# MPTT — Mini Push To Talk

> LoRa 对讲机 v0.1 | STM32WLE5 + WM8960 | 2层 FR-4 35×55mm
> 作者: vam-polf | 2026-06-07

## 项目简介

MPTT (Mini Push To Talk) 是一款基于 E77-400M22S (STM32WLE5) 和 WM8960 音频 Codec 的 LoRa 手持对讲机。单节锂电池供电，USB-C 充电，SMA 外接天线。

## 目录结构

```
hardware/                         ← 硬件设计
├── lora_waikie_2026-06-05.epro2 EasyEDA Pro 工程文件
├── mptt_schematic_v0.1.pdf      原理图 PDF 导出
├── BOM.md                        物料清单 (53项)
├── SCHEMATIC.md                  原理图模块说明
├── PCB.md                        PCB 布局说明
└── NETLIST.md                    网络连接表 (40网络)

docs/                             ← 设计文档
└── DESIGN_v0.1.md                v0.1 完整硬件设计文档 (10章)

reference/datasheets/             ← 芯片数据手册
├── E77-400MBL-01-PIN.xlsx
├── E77-xxxM22S_Usermanual_CN_V1.4.pdf
├── STM32WLE5.xlsx
└── STM32WLE5XX.pdf
```

## 核心器件

| 位号 | 型号 | 功能 |
|------|------|------|
| U1 | E77-400M22S | STM32WLE5 LoRa 模组, 400-470MHz |
| U2 | WM8960CGEFL/RV | 音频 Codec + 1W D类功放 |
| U3 | ME6211C33M5G-N | 3.3V 500mA LDO |
| U4 | TP4056-42-ESOP8 | 锂电池充电管理 |
| U5 | DW01A + U6 8205A | 电池保护 + MOSFET 开关 |

## 设计工具

- **EDA**: 立创EDA专业版 V3.2.121
- **PCB 生产**: JLCPCB (2层 FR-4, 35×55mm)
- **SMT 贴片**: JLCPCB 标准型 (只贴顶层)

## 许可证

MIT License
