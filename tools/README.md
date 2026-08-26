# 工具脚本

## gen_vendor_testpage.py — 厂商风格测试页

生成带外框、标题、参数、灰度渐变/阶梯、网格、对角线、时间戳的标准测试页。

```bash
python3 gen_vendor_testpage.py /tmp/testpage.ras
# 然后通过过滤器打印：
./rastertolhplh 1 test testpage 1 "" /tmp/testpage.ras > /tmp/testpage.lhplh
cat /tmp/testpage.lhplh > /dev/usb/lp0
```

**说明**：
- 输出 CUPS Raster (4651×6627)，过滤器自动加顶部 128 行 padding
- 打印机物理映射（实测修正）：画布 x=0 → 纸张 4.08mm，1px = 0.04219mm
- 方框左右各 10mm，文字中心对齐纸张中心 105mm
- 依赖：ghostscript + DroidSansFallbackFull 字体（中文字体）
