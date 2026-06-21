#!/usr/bin/env python3
"""只读探测 MPTT 设备状态 (不读 SPI2_DR 以免清 RXNE)"""
import sys
from pyocd.core.helpers import ConnectHelper

# 寄存器地址
SPI2_CR1      = 0x40003800
SPI2_SR       = 0x40003808
SPI2_DR       = 0x4000380C   # 不读!
SPI2_I2SCFGR  = 0x4000381C
SPI2_I2SPR    = 0x40003820
GPIOA_BASE    = 0x48000000
GPIOB_BASE    = 0x48000400
RCC_CCIPR     = 0x58000088
RCC_APB1ENR1  = 0x58000058

RESULT_ADDR   = 0x20000100
DIAG_ADDR     = 0x20000110
AUDIO_BUF     = 0x20008000
RAW_DUMP      = 0x20009000

def rd32(s, a):
    return s.read32(a)

def main():
    with ConnectHelper.session_with_chosen_probe(target="stm32wle5cbux") as session:
        m = session.board.target
        def r(a): return m.read32(a)
        def rb(a, n): return [m.read8(a+i) for i in range(n)]
        def rw(a, n): return [m.read32(a+4*i) for i in range(n)]

        print("===== RESULT[0..15] @ 0x20000100 =====")
        res = rb(RESULT_ADDR, 16)
        print(" ".join(f"{x:02X}" for x in res))
        print(f"  [0]=boot(应AA)={res[0]:02X}  [1]=i2c_err={res[1]:02X}  "
              f"[2]=i2s(应55)={res[2]:02X}  [3]=state={res[3]}  "
              f"[4:6]=buf_len={res[4]|(res[5]<<8)}  [6]=idx={res[6]}")

        print("\n===== DIAG[0..7] @ 0x20000110 =====")
        dg = rw(DIAG_ADDR, 8)
        for i,v in enumerate(dg):
            print(f"  DIAG[{i}] = 0x{v:08X}")

        print("\n===== SPI2 寄存器 (不读 DR) =====")
        cr1 = r(SPI2_CR1); sr = r(SPI2_SR); cfgr = r(SPI2_I2SCFGR); pr = r(SPI2_I2SPR)
        print(f"  CR1      = 0x{cr1:08X}  (SPE bit6={cr1>>6&1})")
        print(f"  SR       = 0x{sr:08X}")
        print(f"  I2SCFGR  = 0x{cfgr:08X}")
        print(f"    I2SMOD={cfgr>>11&1} I2SE={cfgr>>10&1} I2SCFG={cfgr>>8&3} "
              f"PCMSYNC={cfgr>>7&1} I2SSTD={cfgr>>4&3} CKPOL={cfgr>>3&1} "
              f"DATLEN={cfgr>>1&3} CHLEN={cfgr>>0&1}")
        print(f"  I2SPR    = 0x{pr:08X}  (MCKOE={pr>>9&1} ODD={pr>>8&1} DIV={pr&0xFF})")

        print("\n===== GPIOA (PA8=CK PA9=WS PA10=SD PA3=MCLK) =====")
        moder = r(GPIOA_BASE+0x00); otyper=r(GPIOA_BASE+0x04)
        ospeed=r(GPIOA_BASE+0x08); pupdr=r(GPIOA_BASE+0x0C)
        idr=r(GPIOA_BASE+0x10); odr=r(GPIOA_BASE+0x14)
        afrl=r(GPIOA_BASE+0x20); afrh=r(GPIOA_BASE+0x24)
        print(f"  MODER =0x{moder:08X}  PA3={moder>>6&3} PA8={moder>>16&3} "
              f"PA9={moder>>18&3} PA10={moder>>20&3}")
        print(f"  OTYPER=0x{otyper:08X}  PA8={otyper>>8&1} PA9={otyper>>9&1} PA10={otyper>>10&1}")
        print(f"  OSPEED=0x{ospeed:08X}")
        print(f"  PUPDR =0x{pupdr:08X}  PA10={pupdr>>20&3}")
        print(f"  IDR   =0x{idr:08X}  PA8={idr>>8&1} PA9={idr>>9&1} PA10={idr>>10&1}")
        print(f"  ODR   =0x{odr:08X}")
        print(f"  AFRL  =0x{afrl:08X}  (PA3 AF={afrl>>12&0xF})")
        print(f"  AFRH  =0x{afrh:08X}  (PA8 AF={afrh>>0&0xF} PA9 AF={afrh>>4&0xF} PA10 AF={afrh>>8&0xF})")

        print("\n===== RCC =====")
        print(f"  CCIPR     =0x{r(RCC_CCIPR):08X}  (SPI2S2SEL={r(RCC_CCIPR)>>10&3})")
        print(f"  APB1ENR1  =0x{r(RCC_APB1ENR1):08X}")

        print("\n===== AUDIO_BUF[0..15] (前16个 int16) =====")
        ab = [m.read16(AUDIO_BUF+2*i) for i in range(16)]
        signed = [v-65536 if v>=32768 else v for v in ab]
        print("  ", " ".join(f"{v:+6d}" for v in signed))

        print("\n===== RAW_DUMP 头部 @ 0x20009000 =====")
        raw = [m.read16(RAW_DUMP+2*i) for i in range(16)]
        raws = [v-65536 if v>=32768 else v for v in raw]
        print("  ", " ".join(f"{v:+6d}" for v in raws))
        cnt = m.read16(RAW_DUMP+4000)
        print(f"  存储的 L 样本数: {cnt}")

if __name__ == "__main__":
    main()
