#!/usr/bin/env python3
# 读取 DIAG 原始采集统计; 若固件启用了 RAW_MIRROR 则导出 WAV (当前主固件未启用镜像)
import struct, math
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

OUT = Path(__file__).resolve().parent / "output"
OUT.mkdir(exist_ok=True)

MIRROR = 0x20002000
DIAG   = 0x20000110
FS = 8930

def s16(v): return v - 0x10000 if v >= 0x8000 else v

with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
    t = session.target
    t.halt()
    rn = t.read32(DIAG + 4 * 13)
    rn = min(rn, 4000) if rn else 0
    raw = [s16(t.read16(MIRROR + 2 * i)) for i in range(rn)] if rn else []
    t.resume()

if not raw:
    print("镜像为空(DIAG[13]=0): 请先完整按一次PTT录音放音, 再运行")
    raise SystemExit

N = len(raw)
dc = sum(raw) / N
ac = [v - dc for v in raw]
peak = max(abs(v) for v in ac)
rms = math.sqrt(sum(x*x for x in ac) / N)
print(f"原始镜像: {N}样本 ({N/FS:.2f}s)  DC={dc:.0f} 峰值={peak:.0f} RMS={rms:.0f}")
print(f"  (峰值≥32000=采集削波; RMS反映人声强度)")

def write_wav(fn, samples, fs):
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples)
    with open(fn, "wb") as f:
        f.write(b"RIFF"); f.write(struct.pack("<I", 36 + len(data))); f.write(b"WAVE")
        f.write(b"fmt "); f.write(struct.pack("<IHHIIHH", 16, 1, 1, fs, fs*2, 2, 16))
        f.write(b"data"); f.write(struct.pack("<I", len(data))); f.write(data)

# 适度归一(放大到峰值约24000)便于电脑听
g = 24000 / peak if peak > 0 else 1
out = OUT / "raw_mic.wav"
write_wav(out, [v * g for v in ac], FS)
print(f"已存 {out} (原始麦克风采集, 放大{g:.1f}x, 处理前)")

# 频谱
def goe(x, f):
    w = 2*math.pi*f/FS; c = 2*math.cos(w); s1=s2=0
    for v in x: s0=v+c*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(s1*s1+s2*s2-c*s1*s2, 0))/len(x)
print("\n频谱(原始):")
tot = sum(goe(ac, f) for f in range(50, 4001, 50)) or 1
for f in [50,100,150,200,300,400,500,700,1000,1500,2000,2500,3000,3500,4000]:
    if f < FS/2:
        e = goe(ac, f)
        bar = "#" * int(40*e/ (peak/10 if peak else 1))
        print(f"  {f:5d}Hz: {e:7.1f} {bar}")
