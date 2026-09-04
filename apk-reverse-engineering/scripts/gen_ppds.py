#!/usr/bin/env python3
# Generate the 6 lenovo PPDs from drv_m100d.dat section (single source of truth).
# Fixes: app builds option arrays from PPD groups (standard names, in section order);
# PPD must expose >= dat-row-range options per group or ArrayIndexOutOfBounds.
import re, sys

DAT = "/root/apk_analysis/original_clean/decoded/assets/data/drv_m100d.dat"
OUT = "/root/apk_analysis/original_clean/decoded/assets"

MODELS = ["M100D", "L100D", "L100DW", "M100DNA", "M1520D", "M1688DW"]

lines = open(DAT).read().splitlines()
# section header = the line matching known block names
hdr_i = next(i for i, l in enumerate(lines) if l.strip() == "PageSize|MediaType|InputSlot|Resolution|ColorModel|Duplex")
blocks, cur = [], []
for l in lines[hdr_i + 1:]:
    if l.strip() == "":
        if cur: blocks.append(cur); cur = []
    else:
        cur.append(l)
if cur: blocks.append(cur)
names = ["PageSize", "MediaType", "InputSlot", "Resolution", "ColorModel", "Duplex"]
sec = dict(zip(names, blocks))

# PageSize: keep first occurrence per name (app used first-pass margins: W=4780 matched 10.75/15)
ps, seen = [], set()
for l in sec["PageSize"]:
    p = l.split("|")
    if p[0] not in seen:
        seen.add(p[0]); ps.append(p)  # name|label|W H|l b r t

# sanity: counts must cover dat-row ranges (PageSize 0-22, MediaType 0-13, InputSlot 0-1, ...)
need = {"PageSize": 23, "MediaType": 14, "InputSlot": 2, "Resolution": 1, "ColorModel": 1, "Duplex": 2}
for k, n in need.items():
    if len(sec[k]) < n: sys.exit(f"FATAL: {k} has {len(sec[k])} < {n}")

def esc(s): return s.replace('"', '\\"')

def ppd(model):
    h = []
    h += ['*PPD-Adobe: "4.3"', '*FileVersion: "1.0-arm"', '*FormatVersion: "4.3"',
          '*LanguageEncoding: ISOLatin1', '*LanguageVersion: English', '',
          f'*Manufacturer: "Lenovo"',
          f'*PCFileName: "Lenovo {model} ARM.PPD"',
          f'*Product: "(Lenovo {model})"',
          '*PSVersion: "(3015.103) 1"',
          f'*ShortNickName: "Lenovo {model} (ARM)"',
          f'*ModelName: "Lenovo {model} ARM"',
          f'*NickName: "Lenovo {model} ARM v2.0 (open-source filter)"',
          '*cupsFilter: "application/vnd.cups-raster 50 rastertolhplh"',
          '*cupsFilter2: "application/vnd.cups-raster application/vnd.cups-raster 50 rastertolhplh"',
          '*cupsFilter2: "image/pwg-raster application/vnd.cups-raster 50 rastertolhplh"', '',
          '*% == Device Identification ==',
          f'*1284DeviceID: "MFG:Lenovo;MDL:{model};DES:Lenovo {model};CMD:GDI;CLS:PRINTER;"', '',
          '*% == Device Capabilities ==',
          '*LanguageLevel: "3"', '*Protocols: TBCP', '*ColorDevice: False',
          '*DefaultColorSpace: Grayscale', '*VariablePaperSize: True',
          '*LandscapeOrientation: Plus90', '*TTRasterizer: Type42',
          '*Throughput: "25"', '*FileSystem: True', '']

    # PageSize / PageRegion / ImageableArea / PaperDimension in section order
    h += ['*% == Paper Sizes ==', '*OpenUI *PageSize/Media Size: PickOne',
          '*OrderDependency: 10 AnySetup *PageSize', '*DefaultPageSize: A4']
    for name, label, wh, marg in ps:
        h.append(f'*PageSize {name}/{esc(label)}: "<</PageSize[{wh}]/ImagingBBox null>>setpagedevice"')
    h += ['*CloseUI: *PageSize', '',
          '*OpenUI *PageRegion/Media Size: PickOne',
          '*OrderDependency: 10 AnySetup *PageRegion', '*DefaultPageRegion: A4']
    for name, label, wh, marg in ps:
        h.append(f'*PageRegion {name}/{esc(label)}: "<</PageSize[{wh}]/ImagingBBox null>>setpagedevice"')
    h += ['*CloseUI: *PageRegion', '', '*DefaultImageableArea: A4']
    for name, label, wh, marg in ps:
        h.append(f'*ImageableArea {name}/{esc(label)}: "{marg}"')
    h += ['', '*DefaultPaperDimension: A4']
    for name, label, wh, marg in ps:
        h.append(f'*PaperDimension {name}/{esc(label)}: "{wh}"')
    maxw = max(float(wh.split()[0]) for _, _, wh, _ in ps)
    maxh = max(float(wh.split()[1]) for _, _, wh, _ in ps)
    h += ['', f'*MaxMediaWidth: "{maxw:.0f}"', f'*MaxMediaHeight: "{maxh:.0f}"',
          '*HWMargins: 10.75 15.00 10.75 15.00', '',
          '*CustomPageSize True: "pop pop pop <</PageSize[5 -2 roll]/ImagingBBox null>>setpagedevice"',
          '*ParamCustomPageSize Width: 1 points 283.4646 612',
          '*ParamCustomPageSize Height: 2 points 360 842',
          '*ParamCustomPageSize WidthOffset: 3 points 0 0',
          '*ParamCustomPageSize HeightOffset: 4 points 0 0',
          '*ParamCustomPageSize Orientation: 5 int 0 0',
          '*LeadingEdge Short: ""', '*DefaultLeadingEdge: Short', '']

    # MediaType (14)
    h += ['*% == Media Type ==', '*OpenUI *MediaType/Paper Type: PickOne',
          '*OrderDependency: 13 AnySetup *MediaType', '*DefaultMediaType: NORMAL']
    for i, l in enumerate(sec["MediaType"]):
        name, label = l.split("|")[0], l.split("|")[1]
        h.append(f'*MediaType {name}/{esc(label)}: "<</MediaType({name})/cupsMediaType {i}>>setpagedevice"')
    h += ['*CloseUI: *MediaType', '']

    # InputSlot (7) — was missing entirely: root cause of length=1;index=1
    h += ['*% == Paper Source ==', '*OpenUI *InputSlot/Paper Source: PickOne',
          '*OrderDependency: 12 AnySetup *InputSlot', '*DefaultInputSlot: Auto']
    for l in sec["InputSlot"]:
        name, label = l.split("|")[0], l.split("|")[1]
        mf = "true" if name == "Manual" else "false"
        h.append(f'*InputSlot {name}/{esc(label)}: "<</ManualFeed {mf}>>setpagedevice"')
    h += ['*CloseUI: *InputSlot', '']

    # Resolution (4) — standard name (was DrvResolution)
    h += ['*% == Resolution ==', '*OpenUI *Resolution/Image Quality: PickOne',
          '*OrderDependency: 20 AnySetup *Resolution', '*DefaultResolution: 600dpi']
    for l in sec["Resolution"]:
        name, label, res = l.split("|")[0], l.split("|")[1], l.split("|")[2]
        x, y = res.split("x")
        h.append(f'*Resolution {name}/{esc(label)}: "<</HWResolution [{x} {y}]>>setpagedevice"')
    h += ['*CloseUI: *Resolution', '']

    # ColorModel (2)
    h += ['*% == Color Model ==', '*OpenUI *ColorModel/Output Color: PickOne',
          '*OrderDependency: 30 AnySetup *ColorModel', '*DefaultColorModel: Gray',
          '*ColorModel Gray/Grayscale: "<</cupsBitsPerPixel 8/cupsBitsPerColor 8/cupsColorSpace 17/NegativePrint false>>setpagedevice"',
          # ponytail: CMYK snippet mirrors Gray params; mono device, CMYK is UI-coverage only
          '*ColorModel CMYK/Color: "<</cupsBitsPerPixel 8/cupsBitsPerColor 8/cupsColorSpace 17/NegativePrint false>>setpagedevice"',
          '*CloseUI: *ColorModel', '']

    # Duplex (3) — standard name (was DrvDuplex)
    h += ['*% == Duplex ==', '*OpenUI *Duplex/Duplex: PickOne',
          '*OrderDependency: 31 AnySetup *Duplex', '*DefaultDuplex: None',
          '*Duplex None/Off (1-Sided): ""',
          '*Duplex DuplexNoTumble/Long-Edge (Portrait): "<</Duplex true/Tumble false>>setpagedevice"',
          '*Duplex DuplexTumble/Short-Edge (Landscape): "<</Duplex true/Tumble true>>setpagedevice"',
          '*CloseUI: *Duplex', '']

    # Toner save (harmless extra)
    h += ['*% == Toner Save ==', '*OpenUI *TonerMode/Toner Saving Mode: PickOne',
          '*OrderDependency: 15.0 AnySetup *TonerMode', '*DefaultTonerMode: 0',
          '*TonerMode 0/Off: "<</TonerMode>>setpagedevice"',
          '*TonerMode 1/On: "<</TonerMode>>setpagedevice"',
          '*CloseUI: *TonerMode', '']
    return "\n".join(h) + "\n"

for m in MODELS:
    path = f"{OUT}/lenovo-{m}-arm.ppd"
    open(path, "w").write(ppd(m))
    n_pagesize = sum(1 for l in open(path) if l.startswith("*PageSize "))
    print(f"{path}: PageSize={n_pagesize} groups=InputSlot,Resolution,Duplex,MediaType,ColorModel OK")
print("counts:", {k: len(v) for k, v in sec.items()})
