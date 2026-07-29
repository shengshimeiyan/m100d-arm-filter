#!/bin/bash
# setup-print-server.sh — 将骁龙410配置为CUPS打印服务器
# 功能: 局域网内所有设备可通过IPP/AirPrint打印到M100D
#
# 前提: install.sh 已执行，rastertolhplh过滤器已安装

set -e

echo "============================================"
echo "  M100D CUPS打印服务器配置"
echo "============================================"
echo ""

# ── 1. 配置CUPS监听网络 ──
echo "[1/5] 配置CUPS网络访问..."

# 备份原配置
sudo cp /etc/cups/cupsd.conf /etc/cups/cupsd.conf.bak 2>/dev/null || true

# 写入新配置
sudo tee /etc/cups/cupsd.conf > /dev/null << 'CUPSCONF'
# ── CUPS打印服务器配置 ──

# 监听所有接口（允许局域网访问）
Listen *:631
Listen /run/cups/cups.sock

# 浏览共享（让局域网其他设备发现打印机）
Browsing On
BrowseLocalProtocols dnssd

# 默认策略
DefaultPolicy default

# 访问控制
<Location />
  Order allow,deny
  Allow @LOCAL    # 允许局域网访问
  Allow localhost  # 允许本机访问
</Location>

<Location /admin>
  Order allow,deny
  Allow localhost
  Allow @LOCAL
</Location>

<Location /admin/conf>
  AuthType Default
  Require user @SYSTEM
  Order allow,deny
  Allow localhost
</Location>

<Location /admin/log>
  AuthType Default
  Require user @SYSTEM
  Order allow,deny
  Allow localhost
</Location>

# 共享连接到本机的打印机
<Policy default>
  JobPrivateAccess default
  JobPrivateValues default
  SubscriptionPrivateAccess default
  SubscriptionPrivateValues default

  <Limit Create-Job Print-Job Print-URI Validate-Job>
    Order deny,allow
    Allow @LOCAL
    Allow localhost
  </Limit>

  <Limit Send-Document Send-URI Hold-Job Release-Job Restart-Job Purge-Jobs Set-Job-Attributes Create-Job-Subscription Renew-Subscription Cancel-Subscription Get-Notifications Reprocess-Job Cancel-Current-Job Suspend-Current-Job Resume-Job CUPS-Move-Job CUPS-Get-Document>
    Require user @OWNER @SYSTEM
    Order deny,allow
    Allow @LOCAL
    Allow localhost
  </Limit>

  <Limit CUPS-Add-Modify-Printer CUPS-Delete-Printer CUPS-Add-Modify-Class CUPS-Delete-Class CUPS-Set-Default CUPS-Get-Devices>
    AuthType Default
    Require user @SYSTEM
    Order deny,allow
    Allow localhost
  </Limit>

  <Limit Pause-Printer Resume-Printer Enable-Printer Disable-Printer Pause-Printer-After-Current-Job Hold-New-Jobs Release-Held-New-Jobs Deactivate-Printer Activate-Printer Restart-Printer Shutdown-Printer Startup-Printer Promote-Job Schedule-Job-After Cancel-Jobs CUPS-Accept-Jobs CUPS-Reject-Jobs>
    AuthType Default
    Require user @SYSTEM
    Order deny,allow
    Allow localhost
  </Limit>

  <Limit Cancel-Job CUPS-Authenticate-Job>
    Require user @OWNER @SYSTEM
    Order deny,allow
    Allow @LOCAL
    Allow localhost
  </Limit>

  <Limit All>
    Order deny,allow
    Allow @LOCAL
    Allow localhost
  </Limit>
</Policy>
CUPSCONF

echo "  cupsd.conf 已更新"

# ── 2. 启用打印机共享 ──
echo "[2/5] 启用打印机共享..."

# 确保CUPS共享已开启
sudo cupsctl --share-printers
sudo cupsctl --remote-any
sudo cupsctl --remote-admin

echo "  打印机共享已启用"

# ── 3. 添加M100D打印机 ──
echo "[3/5] 添加M100D打印机..."

# 检查USB打印机是否已连接
if lsusb 2>/dev/null | grep -qi "17ef"; then
    echo "  检测到Lenovo USB打印机"
else
    echo "  ⚠️ 未检测到USB打印机，请确认M100D已通过USB连接"
    echo "  继续配置（USB设备可能稍后连接）..."
fi

# 查找USB设备URI
PRINTER_URI=$(sudo lpinfo -l -v 2>/dev/null | grep -A3 "usb://Lenovo" | grep "URI:" | head -1 | awk '{print $2}')

if [ -z "$PRINTER_URI" ]; then
    # 尝试更宽泛的搜索
    PRINTER_URI=$(sudo lpinfo -l -v 2>/dev/null | grep -A3 "usb://" | grep -i "lenovo\|m100\|17ef" | grep "URI:" | head -1 | awk '{print $2}')
fi

if [ -z "$PRINTER_URI" ]; then
    echo "  未自动检测到USB URI，使用默认值"
    PRINTER_URI="usb://Lenovo/M100D"
fi

echo "  打印机URI: $PRINTER_URI"

# 删除旧配置（如果存在）
sudo lpadmin -x M100D 2>/dev/null || true

# 添加打印机
sudo lpadmin -p M100D \
    -E \
    -v "$PRINTER_URI" \
    -m lenovo-M100D-arm.ppd \
    -o printer-is-shared=true \
    -D "Lenovo M100D" \
    -L "骁龙410打印服务器" \
    -o printer-error-policy=retry-job \
    -o printer-op-policy=default

echo "  打印机已添加并共享"

# ── 4. 配置AirPrint（iOS/macOS自动发现） ──
echo "[4/5] 配置AirPrint..."

# 安装Avahi（mDNS/DNS-SD服务发现）
sudo apt-get install -y avahi-daemon 2>/dev/null || true

# 确保avahi-daemon运行
sudo systemctl enable avahi-daemon 2>/dev/null || true
sudo systemctl start avahi-daemon 2>/dev/null || true

# 创建AirPrint服务文件
AIRPRINT_NAME="Lenovo M100D"
AIRPRINT_PORT=631
AIRPRINT_RP="printers/M100D"

sudo mkdir -p /etc/avahi/services

sudo tee /etc/avahi/services/AirPrint-M100D.service > /dev/null << AIRPRINT
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name replace-wildcards="yes">${AIRPRINT_NAME}</name>
  <service>
    <type>_ipp._tcp</type>
    <subtype>_universal._sub._ipp._tcp</subtype>
    <port>${AIRPRINT_PORT}</port>
    <txt-record>txtvers=1</txt-record>
    <txt-record>qtotal=1</txt-record>
    <txt-record>rp=${AIRPRINT_RP}</txt-record>
    <txt-record>ty=${AIRPRINT_NAME}</txt-record>
    <txt-record>adminurl=http://%h:631/printers/M100D</txt-record>
    <txt-record>note=Lenovo M100D on Snapdragon 410</txt-record>
    <txt-record>priority=0</txt-record>
    <txt-record>product=(Lenovo M100D)</txt-record>
    <txt-record>printer-state=3</txt-record>
    <txt-record>printer-type=0x80204E</txt-record>
    <txt-record>Transparent=T</txt-record>
    <txt-record>Binary=T</txt-record>
    <txt-record>Colour=F</txt-record>
    <txt-record>Duplex=T</txt-record>
    <txt-record>Staple=F</txt-record>
    <txt-record>Copies=T</txt-record>
    <txt-record>Collate=F</txt-record>
    <txt-record>Sorter=F</txt-record>
    <txt-record>pdl=application/octet-stream,application/pdf,application/postscript,image/urf</txt-record>
    <txt-record>URF=none</txt-record>
  </service>
</service-group>
AIRPRINT

sudo systemctl restart avahi-daemon 2>/dev/null || true

echo "  AirPrint服务已配置"

# ── 5. 重启CUPS ──
echo "[5/5] 重启CUPS..."
sudo systemctl restart cups 2>/dev/null || sudo service cups restart 2>/dev/null

# 等待CUPS启动
sleep 2

# ── 验证 ──
echo ""
echo "============================================"
echo "  配置完成！"
echo "============================================"
echo ""

# 获取IP地址
IP_ADDR=$(hostname -I 2>/dev/null | awk '{print $1}')
HOSTNAME=$(hostname 2>/dev/null || echo "snapdragon410")

echo "📡 打印服务器信息:"
echo "  设备名: $HOSTNAME"
echo "  IP地址: $IP_ADDR"
echo ""
echo "🖥️ 管理界面:"
echo "  http://$IP_ADDR:631/admin"
echo "  http://localhost:631/admin"
echo ""
echo "📱 各设备连接方式:"
echo ""
echo "  ┌─ Windows 电脑 ──────────────────────────────"
echo "  │ 设置 → 设备 → 打印机和扫描仪 → 添加打印机"
echo "  │ → 搜索网络打印机 → 选择 \"Lenovo M100D\""
echo "  │ 或手动添加: http://$IP_ADDR:631/printers/M100D"
echo "  └──────────────────────────────────────────────"
echo ""
echo "  ┌─ macOS ─────────────────────────────────────"
echo "  │ 系统偏好设置 → 打印机与扫描仪 → 添加"
echo "  │ → 自动发现 \"Lenovo M100D\" (AirPrint)"
echo "  └──────────────────────────────────────────────"
echo ""
echo "  ┌─ iPhone / iPad ────────────────────────────"
echo "  │ 分享按钮 → 打印 → 选择打印机"
echo "  │ → 自动发现 \"Lenovo M100D\" (AirPrint)"
echo "  └──────────────────────────────────────────────"
echo ""
echo "  ┌─ Android 手机 ─────────────────────────────"
echo "  │ 安装 \"CUPS Print\" 等APP"
echo "  │ 添加打印机: http://$IP_ADDR:631/printers/M100D"
echo "  └──────────────────────────────────────────────"
echo ""
echo "  ┌─ Linux ─────────────────────────────────────"
echo "  │ lpadmin -p M100D-remote -E \\"
echo "  │   -v ipp://$IP_ADDR:631/printers/M100D"
echo "  │ echo test | lp -d M100D-remote"
echo "  └──────────────────────────────────────────────"
echo ""
echo "🧪 测试打印:"
echo "  echo 'Hello M100D!' | lp -d M100D"
echo "  lpstat -t    # 查看状态"
echo ""
echo "📋 故障排查:"
echo "  cat /var/log/cups/error_log | tail -50"
echo "  sudo cupsctl --debug-logging  # 开启调试日志"
