# MPTT — Multi-Purpose Talkie Terminal

> LoRa 对讲机 | STM32WLE5 + WM8960 | 2层 FR-4 PCB

## 目录结构

```
hardware/                        ← 当前版本 v2.5 硬件设计
├── lora_waikie20260605.eprj2    EasyEDA Pro 工程文件
├── BOM.md                       物料清单 (53项)
├── SCHEMATIC.md                 原理图说明 (6大模块)
├── PCB.md                       PCB布局说明
└── NETLIST.md                   网络连接表 (40网络)

docs/                            ← 设计文档
└── intercom_connection_analysis.md  早期方案分析

reference/datasheets/            ← 芯片数据手册
├── E77-400MBL-01-PIN.xlsx
├── E77-xxxM22S_Usermanual_CN_V1.4.pdf
├── STM32WLE5.xlsx / .pdf
└── INMP4411772809609773.pdf

archive/                         ← 历史版本
├── v1.0/                       初版 (lora_waikie.eprj2)
└── v2.0_kicad/                 KiCad 探索期中间产物

tools/                           ← 辅助脚本
├── generate_pdf.bat / .py
```

## 核心器件

| 位号 | 型号 | 功能 |
|------|------|------|
| U1 | E77-400M22S | STM32WLE5 LoRa 模组 |
| U2 | WM8960CGEFL/RV | 音频 Codec + 1W D类功放 |
| U3 | ME6211C33M5G-N | 3.3V LDO |
| U4 | TP4056-42-ESOP8 | 锂电池充电 |
| U5 | DW01A | 电池保护 |
| U6 | 8205A | 双 N-MOSFET |

## 设计工具

- **EDA**: 立创EDA专业版 V3.2.121
- **生产**: JLCPCB (2层 FR-4, 35×55mm)
