import sys
#!/usr/bin/env python3
# gen_vendor_testpage.py — 厂商风格完整测试页
# 布局：外框 + 标题条 + 参数 + 灰度渐变/阶梯 + 网格 + 对角线 + 时间戳
# 输出：CUPS raster (4651×6627, filter 自动加 padding 128)
# 用法: gen_vendor_testpage.py <输出ras>
import struct, zlib, subprocess, os, datetime, tempfile

# ── 常量（打印机实测映射，用户手工测量修正）──
PRINT_W, PRINT_H = 4651, 6627        # 可打印区（filter 加 padding 128 → 6755）
# 纸张映射: 纸张_mm = 4.08 + 0.04219 × 画布_x ; y: 画布0=纸张11.08mm
MM_PER_PX = 0.04219
OFF_X_MM = 4.08
TOP_MM = 11.08
PAD_PX = 128

FILTER_LEFT_PAD = 67  # filter: left_pad = 2392 - width/2 (width=4651)
def mm2x(mm):
    # 画布坐标 → raster 内坐标（filter 会加 FILTER_LEFT_PAD）
    return int((mm - OFF_X_MM) / MM_PER_PX) - FILTER_LEFT_PAD
def mm2y(mm):
    return int((mm - TOP_MM) / MM_PER_PX) - PAD_PX

# ── 1. 生成文字 PDF（标题 + 参数 + 时间 + 页脚）──
def make_text_pdf(rows, font_path, pdf_path):
    """rows: list of (y_pt, size, text)；y_pt 为 PDF 坐标（底部原点）"""
    ttf = open(font_path, 'rb').read()
    # 简化：复用固定字体对象（CID Droid + Helvetica）
    def parse_cmap(path):
        data = open(path, 'rb').read()
        nt = struct.unpack('>H', data[4:6])[0]
        cmap_off = None
        for i in range(nt):
            if data[12+i*16:12+i*16+4] == b'cmap':
                cmap_off = struct.unpack('>I', data[12+i*16+8:12+i*16+12])[0]
        cmap = data[cmap_off:]
        n = struct.unpack('>H', cmap[2:4])[0]
        gid = {}
        for i in range(n):
            pid, eid, off = struct.unpack('>HHI', cmap[4+i*8:4+i*8+8])
            fmt = struct.unpack('>H', data[cmap_off+off:cmap_off+off+2])[0]
            if fmt == 4:
                sub = data[cmap_off+off:]
                segX2 = struct.unpack('>H', sub[6:8])[0]
                segCount = segX2//2
                endCode = [struct.unpack('>H', sub[14+i*2:16+i*2])[0] for i in range(segCount)]
                startCode = [struct.unpack('>H', sub[16+segX2+i*2:18+segX2+i*2])[0] for i in range(segCount)]
                idDelta = [struct.unpack('>h', sub[18+2*segX2+i*2:20+2*segX2+i*2])[0] for i in range(segCount)]
                idRO = [struct.unpack('>H', sub[20+2*segX2+i*2:22+2*segX2+i*2])[0] for i in range(segCount)]
                base = 22+2*segX2
                for seg in range(segCount):
                    for c in range(startCode[seg], endCode[seg]+1):
                        if c == 0xFFFF: continue
                        if idRO[seg] == 0: g = (c + idDelta[seg]) & 0xFFFF
                        else:
                            idx = idRO[seg]//2 + (c - startCode[seg]) - (segCount - seg)
                            g = struct.unpack('>H', sub[base+idx*2:base+idx*2+2])[0]
                            if g: g = (g + idDelta[seg]) & 0xFFFF
                        gid[c] = g
            elif fmt == 12:
                sub = data[cmap_off+off:]
                ng = struct.unpack('>I', sub[12:16])[0]
                for k in range(ng):
                    s, e, sg = struct.unpack('>III', sub[16+k*12:28+k*12])
                    for c in range(s, min(e+1, 0x110000)):
                        gid[c] = sg + (c - s)
        return gid
    gid = parse_cmap(font_path)
    c2g = bytearray(65536*2)
    for c in range(65536):
        c2g[c*2:c*2+2] = struct.pack('>H', gid.get(c, 0))
    c2g_c = zlib.compress(bytes(c2g), 9)
    font_c = zlib.compress(ttf, 9)

    HELV_W = {' ':0.278,'/':0.278,':':0.278,'-':0.333,'.':0.278,',':0.278,
              '0':0.556,'1':0.556,'2':0.556,'3':0.556,'4':0.556,'5':0.556,
              '6':0.556,'7':0.556,'8':0.556,'9':0.556,'(':0.333,')':0.333,
              '+':0.584,'=':0.584,'_':0.556,'@':1.015,'!':0.278,'?':0.556}
    def is_latin(ch): return 0x20 <= ord(ch) <= 0x7E
    def lat_w(s): return sum(HELV_W.get(c, 0.5) for c in s)

    content = []
    for y_pt, size, text in rows:
        segs = []
        cur = None; buf = []
        for ch in text:
            f = 'L' if is_latin(ch) else 'C'
            if cur is None: cur = f
            if f != cur:
                segs.append((cur, ''.join(buf))); cur = f; buf = []
            buf.append(ch)
        if buf: segs.append((cur, ''.join(buf)))
        cx = 48.0
        for f, s in segs:
            if f == 'C':
                hx = s.encode('utf-16-be').hex().upper()
                content.append(f'BT /F1 {size} Tf {cx:.2f} {y_pt} Td <{hx}> Tj ET')
                cx += len(s) * size
            else:
                esc = s.replace('\\', '\\\\').replace('(', '\\(').replace(')', '\\)')
                content.append(f'BT /F2 {size} Tf {cx:.2f} {y_pt} Td ({esc}) Tj ET')
                cx += lat_w(s) * size
    content_str = '\n'.join(content)
    cb = content_str.encode('latin-1')
    objs = []
    def add(o): objs.append(o); return len(objs)
    add('<< /Type /Catalog /Pages 2 0 R >>')
    add('<< /Type /Pages /Kids [3 0 R] /Count 1 >>')
    add('<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] '
        '/Resources << /Font << /F1 4 0 R /F2 10 0 R >> >> /Contents 5 0 R >>')
    add('<< /Type /Font /Subtype /Type0 /BaseFont /DroidSansFallbackFull '
        '/Encoding /Identity-H /DescendantFonts [6 0 R] >>')
    add(f'<< /Length {len(cb)} >> stream\n{content_str}\nendstream')
    add('<< /Type /Font /Subtype /CIDFontType2 /BaseFont /DroidSansFallbackFull '
        '/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> '
        '/FontDescriptor 7 0 R /DW 1000 /CIDToGIDMap 9 0 R >>')
    add('<< /Type /FontDescriptor /FontName /DroidSansFallbackFull /Flags 4 '
        '/FontBBox [-100 -300 1100 900] /ItalicAngle 0 /Ascent 800 /Descent -200 '
        '/CapHeight 700 /StemV 80 /FontFile2 8 0 R >>')
    add(f'<< /Length {len(font_c)} /Filter /FlateDecode /Length1 {len(ttf)} >> '
        f'stream\n{font_c.decode("latin-1")}\nendstream')
    add(f'<< /Length {len(c2g_c)} /Filter /FlateDecode /Length1 {len(c2g)} >> '
        f'stream\n{c2g_c.decode("latin-1")}\nendstream')
    add('<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>')
    out = bytearray(b'%PDF-1.4\n')
    offs = []
    for i, o in enumerate(objs, 1):
        offs.append(len(out)); out += f'{i} 0 obj\n{o}\nendobj\n'.encode('latin-1')
    xp = len(out)
    out += f'xref\n0 {len(objs)+1}\n'.encode() + b'0000000000 65535 f \n'
    for o in offs: out += f'{o:010d} 00000 n \n'.encode()
    out += f'trailer\n<< /Size {len(objs)+1} /Root 1 0 R >>\nstartxref\n{xp}\n%%EOF\n'.encode()
    open(pdf_path, 'wb').write(out)

# ── 2. 主流程 ──
def main(out_ras, font_path):
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    # PDF 文字行（y_pt 底部原点；842=页面顶）
    rows = [
        (805, 22, 'Lenovo M100D'),
        (775, 13, 'Printer Test Page / 打印机测试页'),
        (745, 10, '==================================================='),
        (710, 12, 'Model / 型号: Lenovo M100D'),
        (690, 12, 'USB ID: 17ef:5444'),
        (670, 12, 'Resolution / 分辨率: 600 dpi'),
        (650, 12, 'Protocol / 协议: LHPL (PJL + JBIG T.85)'),
        (630, 12, 'JBIG / 编码: LRLTWO, MX=8, SDRST'),
        (610, 12, 'Filter / 过滤器: rastertolhplh v3.0.0'),
        (570, 10, '-----------------------------------------------'),
        (540, 11, f'Created / 创建时间: {now}'),
        (520, 11, f'Printed / 打印时间: {now}'),
        (60,  9,  'rastertolhplh v3.0.0 - Lenovo M100D LHPL Test Page'),
    ]
    tmpdir = tempfile.mkdtemp()
    pdf = os.path.join(tmpdir, 'text.pdf')
    pbm = os.path.join(tmpdir, 'text.pbm')
    make_text_pdf(rows, font_path, pdf)
    r = subprocess.run(['gs', '-q', '-sDEVICE=pbmraw', '-r600', '-dNOPAUSE', '-dBATCH',
                        f'-sOutputFile={pbm}', pdf], capture_output=True)
    if r.returncode != 0:
        print('gs error:', r.stderr.decode()[:200]); return
    d = open(pbm, 'rb').read()
    lines = d.split(b'\n')
    i = 1
    while i < len(lines) and (lines[i].startswith(b'#') or lines[i].strip()==b''):
        i += 1
    if len(lines[i].split()) == 1:
        W_IN, H_IN = int(lines[i]), int(lines[i+1]); i += 2
    else:
        W_IN, H_IN = map(int, lines[i].split()); i += 1
    pbm_data = b''.join(lines[i:])
    bpl_in = (W_IN+7)//8
    need = H_IN*bpl_in
    if len(pbm_data) > need: pbm_data = pbm_data[:need]
    elif len(pbm_data) < need: pbm_data = pbm_data + b'\x00'*(need-len(pbm_data))
    print(f'PBM {W_IN}x{H_IN}')

    # 文字边界（页脚 y=60pt 也要，但排除外框——PDF 无外框）
    left = W_IN; right = -1; tfirst = -1; tlast = -1
    for y in range(H_IN):
        row = pbm_data[y*bpl_in:(y+1)*bpl_in]
        for bx in range(bpl_in):
            b = row[bx]
            if b:
                x0 = bx*8
                for x in range(x0, min(x0+8, W_IN)):
                    if b & (0x80 >> (x%8)):
                        if x < left: left = x
                        if x > right: right = x
                        if tfirst < 0: tfirst = y
                        tlast = y
    print(f'文字 x={left}..{right}, y={tfirst}..{tlast}')

    # 缩放：文字区缩放到 raster 顶部（y=mm2y(21) 起），宽度适配
    scale = min((PRINT_W - 160) / (right-left+1), (mm2y(290) - mm2y(21)) / (tlast-tfirst+1))
    W_SC = int(W_IN*scale); H_SC = int(H_IN*scale)
    tleft = int(left*scale); tright = int(right*scale)
    text_w = tright - tleft + 1
    off_x = mm2x(105) - text_w//2   # 文字中心 = 纸张中心 105mm
    # 文字区垂直放在 y=21mm 到 21mm+文字高（页脚在底部单独）
    # 分开处理：主体文字 (y<H_IN/2) 放顶部，页脚 (y>=H_IN/2) 放底部
    split_y = (540/842)*H_IN  # 页脚之前的分隔线
    body_first = -1; body_last = -1
    foot_first = -1; foot_last = -1
    for y in range(H_IN):
        row = pbm_data[y*bpl_in:(y+1)*bpl_in]
        c = sum(bin(b).count('1') for b in row)
        if c > 20:
            if y < split_y:
                if body_first < 0: body_first = y
                body_last = y
            else:
                if foot_first < 0: foot_first = y
                foot_last = y
    print(f'主体文字 y={body_first}..{body_last}, 页脚 y={foot_first}..{foot_last}')

    out = bytearray([255]) * (PRINT_W * PRINT_H)
    # 主体文字：1:1 不缩放，放顶部（21mm 起），水平纸张居中
    body_h = body_last - body_first + 1
    bw = right - left + 1
    box = mm2x(105) - bw//2
    boy = mm2y(21)
    for sy in range(body_h):
        src = (body_first+sy)*bpl_in
        dst = (boy+sy)*PRINT_W + box
        for sx in range(bw):
            if pbm_data[src + (left+sx)//8] & (0x80 >> ((left+sx)%8)):
                if 0 <= dst+sx < len(out): out[dst+sx] = 0
    # 页脚：1:1 放底部（287mm 起），水平纸张居中
    if foot_first >= 0:
        fh = foot_last - foot_first + 1
        ftop = mm2y(287)
        fw = right - left + 1
        fox = mm2x(105) - fw//2
        for sy in range(fh):
            src = (foot_first+sy)*bpl_in
            dst = (ftop+sy)*PRINT_W + fox
            for sx in range(fw):
                if pbm_data[src + (left+sx)//8] & (0x80 >> ((left+sx)%8)):
                    if 0 <= dst+sx < len(out): out[dst+sx] = 0

    # ── 3. 图形元素 ──
    # 外框（粗线 5px）左右 10mm 顶 21mm 底 10mm
    bx0 = mm2x(10); bx1 = mm2x(200)
    by0 = mm2y(21); by1 = mm2y(287)
    bx0 = max(0,bx0); bx1 = min(PRINT_W-1,bx1)
    by0 = max(0,by0); by1 = min(PRINT_H-1,by1)
    for y in range(by0, by1+1):
        for dx in range(5):
            if 0 <= bx0+dx < PRINT_W: out[y*PRINT_W+bx0+dx] = 0
            if 0 <= bx1-dx < PRINT_W: out[y*PRINT_W+bx1-dx] = 0
    for x in range(bx0, bx1+1):
        for dy in range(5):
            if 0 <= by0+dy < PRINT_H: out[(by0+dy)*PRINT_W+x] = 0
            if 0 <= by1-dy < PRINT_H: out[(by1-dy)*PRINT_W+x] = 0

    # 标题条（外框内顶部黑条）
    bar_y0 = by0 + 15
    bar_y1 = by0 + 80
    for y in range(bar_y0, bar_y1+1):
        for x in range(bx0+10, bx1-9):
            out[y*PRINT_W+x] = 0

    # 灰度渐变条（黑→白水平渐变）
    gy0 = 2800
    gy1 = gy0 + 150
    grad_w = (bx1-20) - (bx0+20)
    for x in range(bx0+20, bx1-19):
        v = min(255, int(255 * (x-(bx0+20)) / grad_w))
        for y in range(gy0, gy1+1):
            out[y*PRINT_W+x] = v

    # 灰度阶梯（0/32/64/96/128/160/192/224/255）
    sy0 = gy1 + 100
    sy1 = sy0 + 200
    nblk = 9
    bw2 = (bx1-bx0-40) // nblk
    for k in range(nblk):
        v = min(k*32, 255)
        x0 = bx0+20 + k*bw2
        for y in range(sy0, sy1+1):
            for x in range(x0, x0+bw2):
                out[y*PRINT_W+x] = v

    # 网格区（方框 + 网格）
    gx0 = bx0+20; gy2 = sy1 + 200
    gx1 = bx1-20; gy3 = gy2 + 1000
    for y in range(gy2, gy3+1):
        out[y*PRINT_W+gx0] = 0; out[y*PRINT_W+gx1] = 0
    for x in range(gx0, gx1+1):
        out[gy2*PRINT_W+x] = 0; out[gy3*PRINT_W+x] = 0
    for gx in range(gx0+60, gx1, 60):
        for y in range(gy2, gy3+1):
            out[y*PRINT_W+gx] = 0
    for gy in range(gy2+60, gy3, 60):
        for x in range(gx0, gx1+1):
            out[gy*PRINT_W+x] = 0

    # 对角线（两个对角）
    dx0 = gx0+100; dy0 = gy3 + 200
    dx1 = gx1-100; dy1 = dy0 + 900
    # 主对角线
    n = min(dx1-dx0, dy1-dy0)
    for k in range(n):
        out[(dy0+k)*PRINT_W + dx0+k] = 0
        out[(dy0+k)*PRINT_W + dx1-k] = 0
    # 45° 斜线组
    for off in range(0, n, 100):
        for k in range(min(100, n-off)):
            out[(dy0+off+k)*PRINT_W + dx0+k] = 0
            out[(dy0+off+k)*PRINT_W + dx1-k-off] = 0

    # 写入 raster
    header = bytearray(1796)
    struct.pack_into('<I', header, 0, 0x52615333)
    struct.pack_into('<I', header, 272+4, 0)
    struct.pack_into('<I', header, 276+4, 600)
    struct.pack_into('<I', header, 280+4, 600)
    struct.pack_into('<I', header, 336+4, 0)
    struct.pack_into('<I', header, 340+4, 1)
    struct.pack_into('<I', header, 352+4, 4960)
    struct.pack_into('<I', header, 356+4, 7016)
    struct.pack_into('<I', header, 372+4, PRINT_W)
    struct.pack_into('<I', header, 376+4, PRINT_H)
    struct.pack_into('<I', header, 384+4, 8)
    struct.pack_into('<I', header, 388+4, 8)
    struct.pack_into('<I', header, 392+4, PRINT_W)
    struct.pack_into('<I', header, 400+4, 17)
    with open(out_ras, 'wb') as f:
        f.write(header); f.write(out)
    print(f'raster OK: {out_ras} {PRINT_W}x{PRINT_H}')

if __name__ == '__main__':
    main(sys.argv[1], '/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf')
