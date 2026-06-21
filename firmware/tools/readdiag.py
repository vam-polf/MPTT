#!/usr/bin/env python3
# 只读 DIAG/RESULT, 不复位、不改内存 —— 用于读取用户真实按键说话后的原始采集统计
import sys
from pyocd.core.helpers import ConnectHelper

RESULT_ADDR = 0x20000100
DIAG_ADDR   = 0x20000110

def s16(v):
    return v - 0x10000 if v >= 0x8000 else v

AUDIO_ADDR = 0x20008000

with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
    t = session.target
    t.halt()
    res = [t.read8(RESULT_ADDR + i) for i in range(8)]
    diag = [t.read32(DIAG_ADDR + 4 * i) for i in range(16)]
    # 读处理后缓冲一大段
    NB = 6000
    proc = [s16(t.read16(AUDIO_ADDR + 2 * i)) for i in range(NB)]
    t.resume()

# 处理后缓冲分析(削波率/电平)
clip = sum(1 for v in proc if v >= 32767 or v <= -32768)
pk = max(abs(v) for v in proc)
am = sum(abs(v) for v in proc) / NB
nz = sum(1 for v in proc if abs(v) > 200)
print("== 处理后缓冲分析 ==")
print(f"  峰值={pk}  绝对均值={am:.0f}  非静音样本={nz}/{NB}")
print(f"  削波样本(±满幅)= {clip}/{NB}  ({100*clip/NB:.1f}%)  ← 越高失真越明显")

print("RESULT:", " ".join(f"{b:02X}" for b in res))
print(f"  state(RESULT[3]) = {res[3]}  (0=IDLE 1=REC 2=PLAY)")

dc       = s16(diag[8] & 0xFFFF)
vmin     = s16(diag[9] & 0xFFFF)
vmax     = s16((diag[9] >> 16) & 0xFFFF)
abs_mean = diag[10]
buf_len  = diag[11]
gain     = diag[13]
bmin     = diag[14] & 0xFFFF
bmax     = (diag[14] >> 16) & 0xFFFF
peak = max(abs(vmin), abs(vmax))

print("\n== 原始采集统计 (处理前, 反映麦克风真实信噪比) ==")
print(f"  buf_len      = {buf_len}  ({buf_len/8929:.2f}s)")
print(f"  dc(直流)     = {dc}")
print(f"  原始峰值     = {peak}   (vmin={vmin} vmax={vmax})")
print(f"  原始交流均值 = {abs_mean}")
print("\n== 分块门统计 (处理后写入) ==")
print(f"  最静块(底噪) bmin = {bmin}")
print(f"  最响块(语音) bmax = {bmax}")
print(f"  实际增益 gain     = {gain}")
if bmin > 0:
    print(f"  语音/底噪比 bmax/bmin = {bmax/bmin:.1f}x   (越大越好, <2 几乎分不开)")
print("\n解读:")
if peak < 1500:
    print("  ⚠ 原始峰值很低 → 麦克风采到的人声本身就弱(离嘴远/灵敏度低/增益不足)")
elif bmax > 0 and bmin > 0 and bmax < bmin * 2:
    print("  ⚠ 语音与底噪电平接近 → 信噪比差, 底噪是主要成分(可能是电路噪声)")
else:
    print("  原始电平尚可, 问题更可能在后处理参数")
