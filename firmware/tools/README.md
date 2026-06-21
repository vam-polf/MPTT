# pyocd 调试 / 验证工具

在 `firmware/` 目录下运行:

```bat
python tools\selftest.py      # 自动模拟 PTT, 客观验证通路
python tools\readdiag.py        # 读 DIAG 诊断区 (不复位)
python tools\readproc.py        # 导出处理后音频 → tools\output\proc.wav
python tools\noiseanalyze.py    # 频谱分析
python tools\read_wm.py         # 读 WM8960 shadow (I2C 探测)
python tools\probe.py           # GPIO / 外设快照
```

生成的 WAV 文件写入 `tools/output/` (已 gitignore)。
