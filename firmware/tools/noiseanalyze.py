#!/usr/bin/env python3
# 抓取录音过程中的原始采样并分析噪声性质(频率成分/周期/是否工频)
import time, math
from pyocd.core.helpers import ConnectHelper

RESULT_ADDR = 0x20000100
AUDIO_ADDR  = 0x20008000
GPIOB_BASE  = 0x48000400
BSRR  = GPIOB_BASE + 0x18
MODER = GPIOB_BASE + 0x00
FS = 8930.0   # 单声道存储率

def s16(v): return v - 0x10000 if v >= 0x8000 else v

with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
    t = session.target
    t.reset_and_halt()
    t.resume()
    time.sleep(0.3)                 # 等启动/WM8960 init
    # 模拟按下 PTT: PB8 设为输出并拉低
    t.halt()
    m = t.read32(MODER); m = (m & ~(3 << 16)) | (1 << 16); t.write32(MODER, m)
    t.write32(BSRR, 1 << (8 + 16))  # PB8 = 0 (按下)
    t.resume()
    time.sleep(0.45)                # 录音进行中 (~0.45s, 约4000样本)
    t.halt()
    state = t.read8(RESULT_ADDR + 3)
    n = 3000
    raw = [s16(t.read16(AUDIO_ADDR + 2 * i)) for i in range(n)]
    # 松开 PTT
    t.write32(BSRR, 1 << 8)
    m = t.read32(MODER); m = m & ~(3 << 16); t.write32(MODER, m)
    t.resume()

print(f"state={state} (1=录音中最好) 采到 {n} 个原始样本")
dc = sum(raw) / n
ac = [v - dc for v in raw]
rms = math.sqrt(sum(x * x for x in ac) / n)
peak = max(abs(x) for x in ac)
absmean = sum(abs(x) for x in ac) / n
# 过零率 → 估计主频
zc = sum(1 for i in range(1, n) if (ac[i - 1] < 0) <= (ac[i] < 0) and (ac[i-1]<0)!=(ac[i]<0))
zc = sum(1 for i in range(1, n) if (ac[i-1] < 0) != (ac[i] < 0))
fz = zc * FS / (2 * n)
print(f"DC={dc:.0f}  RMS={rms:.0f}  峰值={peak}  绝对均值={absmean:.0f}")
print(f"过零率主频估计 ≈ {fz:.0f} Hz  (高=高频嘶声, 低=低频嗡声)")

# 手动几个频点的能量(Goertzel), 看是否有工频/特定频率
def goertzel(x, f, fs):
    w = 2 * math.pi * f / fs
    c = 2 * math.cos(w); s0 = s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2; s2 = s1; s1 = s0
    return math.sqrt(max(s1*s1 + s2*s2 - c*s1*s2, 0)) / len(x)

print("\n各频点相对能量:")
for f in [50, 60, 100, 120, 200, 500, 1000, 2000, 3000, 4000]:
    if f < FS/2:
        e = goertzel(ac, f, FS)
        bar = "#" * int(e / max(rms,1) * 40)
        print(f"  {f:5d}Hz: {e:8.1f}  {bar}")

# 模拟固件里的二阶高通(alpha=45/256, 截止≈250Hz), 验证能压掉50Hz
def hpf2(x):
    lp1 = lp2 = 0.0; out = []
    for v in x:
        lp1 += (v - lp1) * 45 / 256; h1 = v - lp1
        lp2 += (h1 - lp2) * 45 / 256; h2 = h1 - lp2
        out.append(h2)
    return out

hp = hpf2(ac)
hp_rms = math.sqrt(sum(v*v for v in hp) / n)
print("\n=== 二阶高通滤波后 (固件同款) ===")
print(f"  RMS: {rms:.0f} → {hp_rms:.0f}  (降噪 {20*math.log10(max(hp_rms,1)/max(rms,1)):.1f} dB)")
print("  各频点能量(滤波后):")
for f in [50, 100, 200, 500, 1000, 2000]:
    if f < FS/2:
        e0 = goertzel(ac, f, FS); e1 = goertzel(hp, f, FS)
        print(f"    {f:5d}Hz: {e0:7.1f} → {e1:7.1f}")

print("\n前40个原始AC样本:")
print(" ".join(f"{v:+5.0f}" for v in ac[:40]))
