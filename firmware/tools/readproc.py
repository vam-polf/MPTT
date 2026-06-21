#!/usr/bin/env python3
# 读取处理后缓冲(0x20008000, 即送给DAC的数字音频), 存WAV供电脑播放对比
# 电脑上播它干净=杂音在喇叭/功放侧; 播它也脏=杂音在数字侧
import struct, math
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

OUT = Path(__file__).resolve().parent / "output"
OUT.mkdir(exist_ok=True)

AUDIO = 0x20008000
DIAG  = 0x20000110
FS = 8930

def s16(v): return v - 0x10000 if v >= 0x8000 else v

with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
    t = session.target
    t.halt()
    buf_len = t.read32(DIAG + 4 * 11)
    n = min(buf_len, 8000) if buf_len else 0
    proc = [s16(t.read16(AUDIO + 2 * i)) for i in range(n)] if n else []
    rawdc  = t.read32(DIAG + 4 * 8)
    rawmm  = t.read32(DIAG + 4 * 9)
    rawam  = t.read32(DIAG + 4 * 10)
    udr    = t.read32(DIAG + 4 * 14)
    iters  = t.read32(DIAG + 4 * 15)
    t.resume()

print(f"播放诊断: 欠载UDR次数={udr}  循环迭代={iters}  (应发样本数≈{2*buf_len})")
print(f"  UDR>0=主循环喂数据跟不上I2S→DAC收到重复/空数据=杂音")

if not proc:
    print(f"缓冲为空(buf_len={buf_len}): 请先完整按一次PTT录音放音")
    raise SystemExit

vmin = s16(rawmm & 0xFFFF); vmax = s16((rawmm >> 16) & 0xFFFF)
print(f"原始采集统计: dc={s16(rawdc)} vmin={vmin} vmax={vmax} 绝对均值={rawam}")
print(f"  (vmin/vmax接近±32767=采集削波)")

N = len(proc)
peak = max(abs(v) for v in proc)
am = sum(abs(v) for v in proc) / N
clip = sum(1 for v in proc if v >= 32767 or v <= -32768)
print(f"处理后: {N}样本({N/FS:.2f}s) 峰值={peak} 绝对均值={am:.0f} 削波={clip}({100*clip/N:.1f}%)")

def write_wav(fn, samples, fs):
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples)
    with open(fn, "wb") as f:
        f.write(b"RIFF"); f.write(struct.pack("<I", 36 + len(data))); f.write(b"WAVE")
        f.write(b"fmt "); f.write(struct.pack("<IHHIIHH", 16, 1, 1, fs, fs*2, 2, 16))
        f.write(b"data"); f.write(struct.pack("<I", len(data))); f.write(data)

out = OUT / "proc.wav"
write_wav(out, proc, FS)
print(f"已存 {out} (= 送给DAC的数字音频, 原样)")

def goe(x, f):
    w = 2*math.pi*f/FS; c = 2*math.cos(w); s1=s2=0
    for v in x: s0=v+c*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(s1*s1+s2*s2-c*s1*s2, 0))/len(x)
mx = max(goe(proc, f) for f in range(50, 4001, 50)) or 1
print("\n处理后频谱:")
for f in [50,100,150,200,300,400,500,700,1000,1500,2000,2500,3000,3500,4000]:
    if f < FS/2:
        e = goe(proc, f)
        print(f"  {f:5d}Hz: {e:8.1f} {'#'*int(60*e/mx)}")
