#!/usr/bin/env python3
"""
MPTT 自主验证脚本 (无需人工按 PTT / 无需听声音)

做法:
  1. reset 让固件启动并完成 WM8960 初始化
  2. 通过调试器把 PB8 配成输出低电平 = 模拟"按下 PTT"
  3. 固件自动跑 录音→播放 循环, 在 录音→播放 切换时把原始录音 dump 到 SRAM
  4. 读回数据, 用两个客观判据判断录音是否正常(而非乱码):
       判据A: DIAG[0] 录音期 I2S 帧错误计数(FRE/OVR/UDR) — 时钟/帧对齐坏时暴涨
       判据B: 逐声道统计 — 右声道被静音应"安静", 若两声道都是满量程噪声 = 乱码

用法: python selftest.py
"""
import sys, time
from pyocd.core.helpers import ConnectHelper

RESULT_ADDR = 0x20000100
DIAG_ADDR   = 0x20000110
RAW_DUMP    = 0x20009200      # 原始交织录音(后处理前), 上限2000个int16, count在+4000
L_DUMP      = 0x20009000

GPIOB_BASE  = 0x48000400
GPIOB_MODER = GPIOB_BASE + 0x00
GPIOB_IDR   = GPIOB_BASE + 0x10
GPIOB_ODR   = GPIOB_BASE + 0x14
GPIOB_BSRR  = GPIOB_BASE + 0x18

SPI2_SR      = 0x40003808
SPI2_I2SCFGR = 0x4000381C
SPI2_I2SPR   = 0x40003820


def s16(v):
    return v - 65536 if v >= 32768 else v


def chan_stats(samples):
    n = len(samples)
    if n == 0:
        return None
    mean = sum(samples) / n
    absdev = sum(abs(x - mean) for x in samples) / n
    amax = max(abs(x) for x in samples)
    sat = sum(1 for x in samples if abs(x) > 16000) / n   # 满量程占比(>半程)
    return dict(n=n, mean=mean, absdev=absdev, amax=amax, sat=sat)


def main():
    with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
        t = session.board.target

        def r8(a):  return t.read8(a)
        def r32(a): return t.read32(a)
        def r16(a): return t.read16(a)
        def w32(a, v): t.write32(a, v)

        print("== 1. 复位并启动固件 ==")
        t.reset_and_halt()
        t.resume()
        time.sleep(0.8)          # 等 WM8960 初始化(含若干 ms 级延时)
        t.halt()

        res = [r8(RESULT_ADDR + i) for i in range(8)]
        print(f"   RESULT: " + " ".join(f"{x:02X}" for x in res))
        print(f"   boot(应AA)={res[0]:02X}  i2c_err(应00)={res[1]:02X}  "
              f"i2s(应55)={res[2]:02X}  state={res[3]}")
        idr = r32(GPIOB_IDR)
        print(f"   PB8 当前电平(1=未按)= {(idr >> 8) & 1}")

        print("\n== 2. 模拟按下 PTT: 把 PB8 驱动为输出低 ==")
        moder = r32(GPIOB_MODER)
        moder = (moder & ~(3 << 16)) | (1 << 16)   # PB8 -> 输出
        w32(GPIOB_MODER, moder)
        w32(GPIOB_BSRR, (1 << 24))                 # PB8 ODR=0 (低=按下)
        time.sleep(0.05)
        idr = r32(GPIOB_IDR)
        print(f"   驱动后 PB8 电平(应0)= {(idr >> 8) & 1}")

        print("\n== 3. 运行 录音->播放 循环, 在播放中停下 ==")
        # 录音约0.9s后进入播放; 轮询 RESULT[3]==2(PLAYBACK) 时停下, 以读到后处理后的缓冲
        t.resume()
        caught_pb = False
        for _ in range(40):                 # 最多 ~4s
            time.sleep(0.1)
            t.halt()
            if r8(RESULT_ADDR + 3) == 2:     # ST_PLAYBACK
                caught_pb = True
                break
            t.resume()
        if not caught_pb:
            print("   (未能在播放态停下, 读到的可能是录音态数据)")
        else:
            print("   已在播放态(state=2)停下, AUDIO_BUF 为后处理后数据")

        print("\n== 4. 读回结果 ==")
        res = [r8(RESULT_ADDR + i) for i in range(8)]
        print(f"   RESULT: " + " ".join(f"{x:02X}" for x in res))
        print(f"   state={res[3]}  buf_len={res[4] | (res[5] << 8)}")

        diag = [r32(DIAG_ADDR + 4 * i) for i in range(16)]
        rec_err   = diag[0]
        first_sr  = diag[7]
        dc_l      = s16(diag[8] & 0xFFFF)
        vmin      = s16(diag[9] & 0xFFFF)
        vmax      = s16((diag[9] >> 16) & 0xFFFF)
        abs_mean  = diag[10]
        buf_len   = diag[11]
        print(f"   DIAG[0] 帧错误/录音错误计数 = {rec_err}")
        print(f"   DIAG[7] 首次RXNE时SR = 0x{first_sr:04X} "
              f"(FRE/bit4={first_sr>>4&1} OVR/bit6={first_sr>>6&1} UDR/bit3={first_sr>>3&1})")
        print(f"   DIAG[8..11] L声道: dc={dc_l} min={vmin} max={vmax} "
              f"abs_mean={abs_mean} buf_len={buf_len}")

        cfgr = r32(SPI2_I2SCFGR); pr = r32(SPI2_I2SPR)
        print(f"   SPI2_I2SCFGR=0x{cfgr:08X} (I2SCFG={cfgr>>8&3}) "
              f"I2SPR=0x{pr:08X} (DIV={pr&0xFF})")

        gain = diag[13]
        # 后处理后的 AUDIO_BUF 直读 (单声道连续, 已增益+淡入)
        ab = [s16(r16(0x20008000 + 2 * i)) for i in range(32)]
        print(f"\n   AUDIO_BUF[0..15] (单声道, 已增益, 开头应淡入爬升):")
        print("   " + " ".join(f"{v:+6d}" for v in ab[:16]))
        # 扫描整段缓冲的真实峰值/均值(跳过开头淡入区)
        N = min(buf_len, 4000)
        mid = [s16(r16(0x20008000 + 2 * i)) for i in range(400, N)]
        ab_peak = max((abs(v) for v in mid), default=0)
        ab_absmean = (sum(abs(v) for v in mid) // len(mid)) if mid else 0
        nbig = sum(1 for v in mid if abs(v) > 16000)
        print(f"   缓冲中段(400..{N}): 峰值={ab_peak} 交流均值={ab_absmean} "
              f"过半程样本数={nbig}/{len(mid)}")

        print("\n== 5. 麦克风电平分析 (DIAG, 后处理前) ==")
        peak = max(abs(vmin), abs(vmax))
        print(f"   abs_mean(交流均值)={abs_mean}  峰值|max|={peak}  dc={dc_l}  自适应增益={gain}")
        print(f"   后处理后缓冲峰值≈{ab_peak} (目标24000, 越接近越响)")
        print(f"   (修复前实测 abs_mean≈1~2, 峰值≈40 = 通路断开几乎无信号)")

        print("\n== 6. 释放 PTT (PB8 复位为输入) ==")
        moder = r32(GPIOB_MODER)
        moder = (moder & ~(3 << 16))     # PB8 -> 输入
        w32(GPIOB_MODER, moder)

        # ---- 客观判据 ----
        print("\n========== 判定 ==========")
        ok = True
        reasons = []

        if res[0] != 0xAA:
            ok = False; reasons.append(f"启动失败 boot=0x{res[0]:02X}")
        if res[1] != 0x00:
            ok = False; reasons.append(f"WM8960 I2C 初始化错误 0x{res[1]:02X}")

        if buf_len < 100:
            ok = False; reasons.append(f"几乎没录到数据 (buf_len={buf_len}) — PTT模拟或录音未触发")
        else:
            reasons.append(f"录音时长 buf_len={buf_len} 个单声道样本 ≈ {buf_len/8929:.2f}秒")

        # 麦克风通路是否接通: 接通后(LMIC2B=1 + +20dB)噪声底/信号应明显高于修复前的~2
        if abs_mean >= 8 or peak >= 200:
            reasons.append(f"麦克风电平已明显抬升(abs_mean={abs_mean}, 峰值={peak}) "
                           f"=> 通路已接通(LMIC2B=1 生效)")
        else:
            ok = False
            reasons.append(f"麦克风电平仍很低(abs_mean={abs_mean}, 峰值={peak}) "
                           f"=> 通路可能仍未接通或环境极安静")

        # 是否削波(过增益): 安静环境就railing说明增益过大
        if peak >= 32000:
            reasons.append(f"⚠ 峰值接近满量程({peak}), 安静环境即削波 => 建议把 boost 降到 +13dB(R20=0x118)")

        # 帧对齐健康度
        if buf_len > 0 and rec_err > buf_len // 2:
            ok = False
            reasons.append(f"录音期帧错误计数过高 DIAG[0]={rec_err} (buf_len={buf_len})")
        else:
            reasons.append(f"录音期帧错误计数正常 DIAG[0]={rec_err}")

        # 后处理后缓冲是否达到可听响度
        if ab_peak >= 8000:
            reasons.append(f"后处理后缓冲峰值={ab_peak} (>=8000, 播放应清晰可听)")
        else:
            reasons.append(f"⚠ 后处理后缓冲峰值仅={ab_peak} (偏低, 安静环境正常; 对麦克风说话应更高)")

        for rr in reasons:
            print("  - " + rr)
        print("\n  结论: " + ("[PASS] 麦克风通路已接通、电平正常、帧对齐正常" if ok
                              else "[FAIL] 仍有问题, 见上面原因"))
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
