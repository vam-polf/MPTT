#!/usr/bin/env python3
# 抓取用户真实说话时的"原始"录音波形, 存成WAV供电脑播放对比, 并做频谱分析
# 用法: 用户按住PTT持续说话, 运行本脚本; 脚本在录音相位抓取原始样本
import time, struct, math
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

OUT = Path(__file__).resolve().parent / "output"
OUT.mkdir(exist_ok=True)

RESULT_ADDR = 0x20000100
AUDIO_ADDR  = 0x20008000
FS = 8930

def s16(v): return v - 0x10000 if v >= 0x8000 else v

with ConnectHelper.session_with_chosen_probe(target_override="stm32wle5cbux") as session:
    t = session.target
    # 暂停式轮询: 反复 halt 读 state, 抓到录音相位(==1)就立即读原始缓冲
    print(">>> 脚本就绪! 现在开始按PTT说话, 20秒内多按几次 <<<")
    caught = False; st = 0; N = 3000
    raw = []
    for _ in range(1500):
        t.halt()
        st = t.read8(RESULT_ADDR + 3)
        if st == 1:
            # 检测到录音开始: 放它实录0.45s再读, 确保读到这次新鲜语音(非残留)
            t.resume()
            time.sleep(0.45)
            t.halt()
            st2 = t.read8(RESULT_ADDR + 3)
            if st2 == 1:           # 仍在录音=抓得早, 现已有0.45s新鲜数据
                raw = [s16(t.read16(AUDIO_ADDR + 2 * i)) for i in range(N)]
                t.resume()
                caught = True
                break
            t.resume()             # 抓晚了(已转播放), 放弃重试
        else:
            t.resume()
        time.sleep(0.01)
    if not caught:
        print("仍未捕获到录音相位; 读当前缓冲(可能是处理后数据)")
        t.halt(); raw = [s16(t.read16(AUDIO_ADDR + 2 * i)) for i in range(N)]; t.resume()

print(f"抓取时 state={st} (1=真原始录音), 取 {len(raw)} 样本")
N = len(raw)
dc = sum(raw) / N
ac = [v - dc for v in raw]
peak = max(abs(v) for v in ac)
rms = math.sqrt(sum(x*x for x in ac)/N)
print(f"DC={dc:.0f} 峰值={peak} RMS={rms:.0f}  (峰值接近32767=采集削波)")

# 存WAV(原始, 未处理) 供电脑播放
def write_wav(fn, samples, fs):
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples)
    with open(fn, "wb") as f:
        f.write(b"RIFF"); f.write(struct.pack("<I", 36 + len(data))); f.write(b"WAVE")
        f.write(b"fmt "); f.write(struct.pack("<IHHIIHH", 16, 1, 1, fs, fs*2, 2, 16))
        f.write(b"data"); f.write(struct.pack("<I", len(data))); f.write(data)

# 原始(去DC, 放大8倍方便听)
write_wav(OUT / "raw_capture.wav", [v*8 for v in ac], FS)
print(f"已存原始录音: {OUT / 'raw_capture.wav'} (已放大8倍)")

# 模拟固件二阶高通 + 简单AGC, 也存一份处理后的
def hpf2(x):
    lp1=lp2=0.0; o=[]
    for v in x:
        lp1+=(v-lp1)*45/256; h1=v-lp1
        lp2+=(h1-lp2)*45/256; h2=h1-lp2
        o.append(h2)
    return o
hp = hpf2(ac)
hp_rms = math.sqrt(sum(x*x for x in hp)/N)
write_wav(OUT / "hpf_capture.wav", [max(-32768,min(32767,v*16)) for v in hp], FS)
print(f"已存高通后录音: {OUT / 'hpf_capture.wav'} (放大16倍)")

# 频谱(Goertzel)
def g(x,f): 
    w=2*math.pi*f/FS; c=2*math.cos(w); s1=s2=0
    for v in x: s0=v+c*s1-s2; s2=s1; s1=s0
    return math.sqrt(max(s1*s1+s2*s2-c*s1*s2,0))/len(x)
print("\n原始频谱 / 高通后:")
for f in [50,100,200,300,500,800,1200,2000,3000,4000]:
    if f<FS/2:
        print(f"  {f:5d}Hz: {g(ac,f):7.1f}  →  {g(hp,f):7.1f}")
print("\n前60个高通后样本(看波形是否规则):")
print(" ".join(f"{int(v):+5d}" for v in hp[:60]))
