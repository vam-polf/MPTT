#!/usr/bin/env python3
"""读取 WM8960 所有可读寄存器 (通过 I2C, addr 0x1A)"""
from pyocd.core.helpers import ConnectHelper

I2C1_BASE = 0x40005400
I2C_CR1   = I2C1_BASE+0x00
I2C_CR2   = I2C1_BASE+0x04
I2C_ISR   = I2C1_BASE+0x04   # OAR1 占用, ISR 在 +0x04? 需核对
# STM32WL I2C1: CR1=0x00 CR2=0x04 OAR1=0x08 OAR2=0x0C TIMINGR=0x10 TIMEOUTR=0x14
#               ISR=0x18 ICR=0x1C PECR=0x20 RXDR=0x24 TXDR=0x28
I2C_CR1   = I2C1_BASE+0x00
I2C_CR2   = I2C1_BASE+0x04
I2C_ISR   = I2C1_BASE+0x18
I2C_ICR   = I2C1_BASE+0x1C
I2C_TXDR  = I2C1_BASE+0x28
I2C_RXDR  = I2C1_BASE+0x24

# WM8960 寄存器名称 (部分关键的)
REG_NAMES = {
    0x00:"R0 LINVOL",0x01:"R1 RINVOL",0x04:"R4 ADCCFG",0x05:"R5 ADCCONTROL",
    0x07:"R7 I2S1",0x08:"R8 CLOCK1",0x09:"R9 ADDCTRL1",0x0A:"R0A LDAC",
    0x0B:"R0B RDAC",0x0F:"R0F RESET",0x15:"R15 LADC",0x16:"R16 RADC",
    0x18:"R18 ADDCTRL2",0x19:"R19 PWR1",0x1A:"R1A PWR2",0x1F:"R1F PWRMGMT2",
    0x20:"R20 LMIC",0x21:"R21 RMIC",0x22:"R22 LMIX",0x25:"R25 RMIX",
    0x28:"R28 LSPK",0x29:"R29 RSPK",0x2F:"R2F PWR3",0x31:"R31 CLASSD",
}

def wm_read(m, reg):
    """读 WM8960 单寄存器: write reg addr, restart, read 1 byte
       WM8960: 7-bit reg addr + 9-bit data → 2 bytes: [reg<<1 | data_msb], [data_lsb]
       读时: 先写 [reg<<1], 再读 2 bytes
    """
    # 停止任何正在进行的传输
    m.write32(I2C_CR1, 0)
    # NOSTRETCH + AUTOEND 配置, 写 1 byte 寄存器地址
    # CR2: START=1, AUTOEND=1, NBYTES=1, WR=0
    m.write32(I2C_CR2, (0x1A<<1) | (1<<16) | (1<<25) | (1<<13))  # slave addr, nbytes=1, autoend, start
    t=50000
    while not (m.read32(I2C_ISR) & (1<<1)) and t>0:  # TXIS
        t-=1
    if t==0: return None
    m.write32(I2C_TXDR, (reg<<1))  # reg addr (7 bit), bit0=0 (data msb=0)
    t=50000
    while not (m.read32(I2C_ISR) & (1<<5)) and t>0:  # STOPF
        t-=1
    m.write32(I2C_ICR, 1<<5)  # clear STOPF
    # 读阶段
    m.write32(I2C_CR2, (0x1A<<1) | (1<<16) | (1<<25) | (1<<10) | (1<<13))  # RD_WRN=1 read
    t=50000
    while not (m.read32(I2C_ISR) & (1<<2)) and t>0:  # RXNE
        t-=1
    if t==0: return None
    b1 = m.read32(I2C_RXDR) & 0xFF
    t=50000
    while not (m.read32(I2C_ISR) & (1<<5)) and t>0:  # STOPF
        t-=1
    m.write32(I2C_ICR, 1<<5)
    # b1 包含: bit8(msb) | 7 bit lsb? 不对, WM8960 读返回 1 byte: 8-bit data
    # 实际 WM8960 读时返回 1 字节 = data[7:0], data[8] 在写时的 addr 字节里
    # 但读操作无法获取 bit8... 这是 WM8960 的限制
    return b1

def main():
    with ConnectHelper.session_with_chosen_probe(target="stm32wle5cbux") as session:
        m = session.board.target
        # 先确保 I2C1 开启 (固件应已开启, 但保险)
        m.write32(I2C_CR1, 1)

        print("WM8960 寄存器读取 (注意: 只能读低 8 位, bit8 不可读):")
        print("-"*60)
        for reg in sorted(REG_NAMES.keys()):
            v = wm_read(m, reg)
            if v is None:
                print(f"  R{reg:02X} {REG_NAMES[reg]:20s}: 读超时")
            else:
                print(f"  R{reg:02X} {REG_NAMES[reg]:20s}: 0x{v:02X} ({v:08b})")

if __name__=="__main__":
    main()
