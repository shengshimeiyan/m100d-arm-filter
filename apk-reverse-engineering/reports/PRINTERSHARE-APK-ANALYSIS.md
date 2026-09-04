# PrinterShare APK 分析报告

## 基本信息
- **应用名称**: PrinterShare
- **包名**: com.dynamixsoftware.printershare
- **版本**: 12.14.1 (versionCode 471)
- **开发者**: Dynamix USA, LLC (© 2009-2023)
- **APK 大小**: 2.1 MB (压缩) / ~4 MB (解压)
- **签名**: v1+v2+v3 全部签名, 证书 CN=editor, 有效期 2016-01-10 至 2115-12-17
- **最小SDK**: 19 (Android 4.4 KitKat)
- **目标SDK**: 33 (Android 13)
- **编译SDK**: 33

## 平台信息
- 文件: 208 个文件
- 类数量: 1989 个 (1025 个在 printershare 包内)
- DEX: 单个 classes.dex (1.9 MB)
- **混淆工具**: Smob v3.3 by Kirlif' (大部分类被混淆为 a/b/c...)

## 应用类型
**移动打印应用程序** - 通过 Wi-Fi、蓝牙、USB 连接到打印机
支持多种打印机品牌的远程打印，包括 HP、Canon、Epson、Brother、Samsung、Star、Zebra 等

## 支持的打印协议
- IPP (Internet Printing Protocol) - 32 处引用
- AirPrint - 2 处引用
- WSD (Web Services for Devices) - 3 处引用
- LPD (Line Printer Daemon) - 5 处引用
- 原始 Socket 打印 - 113 处引用
- TCP/IP、JetDirect
- SNMP (9 处) - 用于发现网络打印机
- Canon 私有协议
- mDNS/Zeroconf (用于 AirPrint 发现)
- WS-Discovery
- WSD Print (Microsoft 协议)

## 支持的打印机品牌
HP (22), Canon (38), Epson (15), Brother (15), Samsung (4), Star (12), Zebra (4), OKI (4), Kyocera (2), Xerox (1), TSC (1), Ricoh (1), Lexmark (1)

## 主要功能 (Activities)
- **ActivityMain** - 主入口 (启动器)
- **ActivityPrintDocuments** - 文档打印 (PDF, DOC, DOCX, XLS, XLSX, PPT, PPTX, HWP, TXT)
- **ActivityPrintPictures** - 图片打印 (BMP, GIF, PNG, JPG, WEBP)
- **ActivityWeb** - 网页打印 (HTML)
- **ActivityGmail** - Gmail 邮件打印
- **ActivityGmailConversation** - Gmail 对话查看
- **ActivityContacts** - 联系人打印
- **ActivityCalendar** - 日历打印
- **ActivityFindPrinters** - 查找打印机
- **ActivityLocalPrinters** - 本地打印机
- **ActivityPrinters** - 打印机列表
- **ActivityUSB** - USB 打印机
- **ActivityProfile** / **ActivityProfileEdit** - 用户配置
- **ActivityKey** - 激活密钥 (URL Scheme: printershare://activate)
- **ActivityPreview** - 打印预览
- **ActivityPrintTestPage** - 打印测试页
- **ActivityDriversBrowser** - 驱动浏览器
- **ActivityPrintCalendar / Contacts / Web / Gmail / Android** - 各类型打印

## 服务
- **AndroidPrintService** - Android 系统打印服务 (BIND_PRINT_SERVICE)
- **com.dynamixsoftware.printershare.FlurryContentProvider** - Flurry 分析

## PDF 渲染引擎
- **libpdfium.so** (Google Chromium 的 PDF 库)
- 通过 JNI 调用: PDFrender 类 (create, destroy, drawPage, getPageCount, getPageSize)
- **K2render** - 文档查看器 JNI 接口 (init, openFile, drawPage, getPageCount, getPageSize, setDPI)
- 包含 16 个 PDF 渲染库 (4 个版本 × 4 个架构: arm/arm64/x86/x86_64)
- 版本: 5.0.3, 6.0.3, 7.0.3, 8.0.3

## 标签打印机支持
- **ZPL** (Zebra Programming Language) - 通过 _zpl 后缀识别
- **EPL** (Eltron Programming Language) - 通过 _epl 后缀识别

## 打印机驱动 (assets/data/)
- **drv_escpr.dat** - Epson ESC/P-R 驱动
- **drv_gutenprint.dat** - Gutenprint 通用驱动
- **drv_hplip.dat** - HP HPLIP 驱动
- **drv_splix.dat** - SPLIX (Samsung) 驱动

## 网络端点
- **https://api.printershare.net/v1/help** - 国际版 API
- **https://api.printershare.cn/v1/help** - 中国版 API
- **https://api.printershare.net/v1/help/sms** - 短信帮助
- **https://play.google.com/store** - Google Play 商店
- **localhost:4349** - 本地诊断 HTTP 服务器 (应用启动时)

## 权限 (23 个)
- INTERNET, WAKE_LOCK, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE
- CHANGE_WIFI_MULTICAST_STATE, READ_PHONE_STATE
- GET_ACCOUNTS, USE_CREDENTIALS, MANAGE_ACCOUNTS (Gmail 访问)
- READ_EXTERNAL_STORAGE, WRITE_EXTERNAL_STORAGE (maxSdk=22)
- READ_CONTACTS, READ_CALENDAR
- BLUETOOTH, BLUETOOTH_ADMIN (maxSdk=30), BLUETOOTH_CONNECT, BLUETOOTH_SCAN
- ACCESS_FINE_LOCATION (maxSdk=30) - 用于发现 WiFi 打印机
- POST_NOTIFICATIONS
- FOREGROUND_SERVICE
- com.android.vending.BILLING (Google Play 内购)
- BIND_PRINT_SERVICE, BIND_JOB_SERVICE

## 内购功能
- 使用 Google Play Billing Client 6.0.1
- Premium 升级功能（去除限制，支持无限打印）
- 免费版限制：WiFi/Bluetooth/USB 打印有特定限制，Windows/Mac 共享打印机无限制

## 文件/邮件支持
- 文档: pdf, doc, docx, docm, xls, xlsx, xlsm, ppt, pptx, pptm, hwp, txt
- 图片: bmp, gif, png, jpg, webp
- 邮件: Gmail 直接访问 (OAuth + 应用专用密码)

## 第三方库
- **androidx.core, androidx.exifinterface, androidx.versionedparcelable**
- **com.flurry.android** - Flurry 分析 SDK
- **com.google.android.play:assetpacks** - Play Asset Delivery (20002)
- **com.google.android.datatransport** - 数据传输 (Firebase/Crashlytics)
- **Google Play Billing Client 6.0.1**

## 加密 / 安全
- DES (6 处), SHA (10 处) 算法引用
- 网络安全配置 (network_security_config.xml)
- 已签名证书 CN=editor (非 Play Store 签名，可能是修改/重打包版本)

## 本地化
- 支持 ~30 种语言 (英语、中文、德语、法语、意大利语、西班牙语、葡萄牙语、俄语、波兰语、捷克语、荷兰语、斯洛文尼亚语、克罗地亚语等)

## 支持的 Intent Filters
- `printershare://activate` - URL Scheme 激活
- `printershare://activation_key` - 激活密钥
- `android.hardware.usb.action.USB_DEVICE_ATTACHED` - USB 设备连接
- `VIEW/SEND/SEND_MULTIPLE` - 各种 MIME 类型
