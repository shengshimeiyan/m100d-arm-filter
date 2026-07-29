#!/bin/bash
# install.sh — 安装 Lenovo M100D ARM原生CUPS过滤器
# 目标设备: 骁龙410 (aarch64) + Debian + 512MB RAM
#
# 两种安装方式:
#   方式1 (预编译静态二进制): 无需编译，直接复制即可
#   方式2 (从源码编译): 需要gcc、make和JBIG-KIT

set -e

FILTER="rastertolhplh"
PPD="lenovo-M100D-arm.ppd"
FILTER_DIR="/usr/lib/cups/filter"
PPD_DIR="/usr/share/ppd/Lenovo"
DEV_DIR="/usr/lib/udev/rules.d"

echo "=== Lenovo M100D ARM原生CUPS过滤器 安装程序 ==="
echo ""

# 1. 检查CUPS是否安装
if ! command -v cupsd &>/dev/null && ! dpkg -l cups &>/dev/null; then
    echo "错误: CUPS未安装。请先安装:"
    echo "  sudo apt-get install cups cups-client"
    exit 1
fi

# 2. 检查架构
ARCH=$(uname -m)
echo "当前架构: $ARCH"

# 3. 选择安装方式
if [ -f "rastertolhplh-aarch64" ] && [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    echo "[1/5] 使用预编译静态二进制 (无需编译)..."
    FILTER_BIN="rastertolhplh-aarch64"
elif [ -f "rastertolhplh" ]; then
    echo "[1/5] 使用已编译的二进制..."
    FILTER_BIN="rastertolhplh"
else
    echo "[1/5] 从源码编译..."
    # 安装编译依赖
    sudo apt-get install -y gcc make libcups2-dev libcupsimage2-dev
    # 下载JBIG-KIT
    if [ ! -d "jbigkit-2.1" ]; then
        wget -q "https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz"
        tar xzf jbigkit-2.1.tar.gz
    fi
    make clean && make
    FILTER_BIN="rastertolhplh"
fi

# 4. 安装过滤器
echo "[2/5] 安装过滤器..."
sudo install -d "$FILTER_DIR"
sudo install -m 755 "$FILTER_BIN" "$FILTER_DIR/$FILTER"

# 5. 安装PPD
echo "[3/5] 安装PPD..."
sudo install -d "$PPD_DIR"
sudo install -m 644 "$PPD" "$PPD_DIR/lenovo-M100D-arm.ppd"

# 6. 安装USB udev规则
echo "[4/5] 安装USB规则..."
sudo install -d "$DEV_DIR"
cat > /tmp/99-lenovo-m100d.rules << 'UDEV'
# Lenovo M100D/M100DNA/L100D/L100DW USB printer rules
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5442", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5443", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5444", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5445", MODE="0666", GROUP="lp"
UDEV
sudo install -m 644 /tmp/99-lenovo-m100d.rules "$DEV_DIR/99-lenovo-m100d.rules"
sudo udevadm control --reload-rules

# 7. 重启CUPS
echo "[5/5] 重启CUPS..."
sudo systemctl restart cups 2>/dev/null || sudo service cups restart 2>/dev/null

echo ""
echo "=== 安装完成! ==="
echo ""
echo "过滤器: $FILTER_BIN ($(stat -c%s "$FILTER_BIN") bytes, 静态链接)"
echo ""
echo "下一步: 添加打印机"
echo "  方法1 (命令行):"
echo "    sudo lpadmin -p M100D -E -v usb://Lenovo/M100D -m lenovo-M100D-arm.ppd"
echo ""
echo "  方法2 (Web界面):"
echo "    打开 http://localhost:631/admin → 添加打印机 → 选择 Lenovo M100D"
echo "    → 选择PPD: Lenovo M100D ARM"
echo ""
echo "  方法3 (GUI):"
echo "    系统设置 → 打印机 → 添加 → Lenovo M100D"
echo ""
echo "测试打印:"
echo "    echo 'Hello M100D!' | lp -d M100D"
echo ""
echo "注意: 本过滤器是逆向工程产物，协议兼容性需在实际打印机上验证。"
