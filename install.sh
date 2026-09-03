#!/bin/bash
# install.sh — 一键编译安装 Lenovo M100D ARM原生CUPS过滤器
# 适用于: 骁龙410 (aarch64) + Debian + 512MB RAM
#
# 用法: sudo bash install.sh
#       bash install.sh --build-only   (仅编译，不安装)

set -e

FILTER="rastertolhplh"
PPD="lenovo-M100D-arm.ppd"
FILTER_DIR="/usr/lib/cups/filter"
PPD_DIR="/usr/share/ppd/Lenovo"
DEV_DIR="/usr/lib/udev/rules.d"

BUILD_ONLY=0
[ "$1" = "--build-only" ] && BUILD_ONLY=1

echo "============================================"
echo "  M100D ARM原生CUPS过滤器 — 编译安装"
echo "============================================"
echo ""

# ── 1. 安装编译依赖 ──
echo "[1/3] 检查编译依赖..."
if ! command -v gcc &>/dev/null; then
    echo "  安装 gcc..."
    sudo apt-get install -y gcc make
fi
echo "  gcc: $(gcc --version 2>/dev/null | head -1)"
echo "  ✓ 编译依赖就绪"

# ── 2. 编译（jbig/ 内置修改版 jbigkit，无需下载）──
echo ""
echo "[2/3] 编译过滤器..."
make clean 2>/dev/null || true
make
echo "  ✓ 编译成功"
echo "  二进制: $(file $FILTER | cut -d: -f2)"
echo "  大小: $(ls -lh $FILTER | awk '{print $5}')"

if [ $BUILD_ONLY -eq 1 ]; then
    echo ""
    echo "=== 编译完成（仅编译模式）==="
    echo "  运行 sudo bash install.sh 来安装"
    exit 0
fi

# ── 3. 安装 ──
echo ""
echo "[3/3] 安装..."

# 安装过滤器
sudo install -d "$FILTER_DIR"
sudo install -m 755 "$FILTER" "$FILTER_DIR/$FILTER"
echo "  ✓ 过滤器 → $FILTER_DIR/$FILTER"

# 安装PPD
sudo install -d "$PPD_DIR"
sudo install -m 644 "$PPD" "$PPD_DIR/lenovo-M100D-arm.ppd"
echo "  ✓ PPD → $PPD_DIR/lenovo-M100D-arm.ppd"

# 安装USB udev规则
sudo install -d "$DEV_DIR"
cat > /tmp/99-lenovo-m100d.rules << 'UDEV'
# Lenovo M100D/M100DNA/L100D/L100DW USB printer rules
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5442", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5443", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5444", MODE="0666", GROUP="lp"
SUBSYSTEM=="usb", ATTR{idVendor}=="17ef", ATTR{idProduct}=="5445", MODE="0666", GROUP="lp"
UDEV
sudo install -m 644 /tmp/99-lenovo-m100d.rules "$DEV_DIR/99-lenovo-m100d.rules"
sudo udevadm control --reload-rules 2>/dev/null || true
echo "  ✓ USB规则 → $DEV_DIR/99-lenovo-m100d.rules"

# 重启CUPS
sudo systemctl restart cups 2>/dev/null || sudo service cups restart 2>/dev/null || true
echo "  ✓ CUPS已重启"

echo ""
echo "============================================"
echo "  ✅ 安装完成！"
echo "============================================"
echo ""
echo "下一步: 添加打印机"
echo "  sudo lpadmin -p M100D -E -v usb://Lenovo/M100D -m lenovo-M100D-arm.ppd"
echo ""
echo "测试打印:"
echo "  echo 'Hello M100D!' | lp -d M100D"
echo ""
echo "配置打印服务器（AirPrint）:"
echo "  sudo bash setup-print-server.sh"
