# Draws the front panel of the MIDI sound module (own artwork, no brand names or logos): assets/models/module-front.png, 1744 x 352.
# The positions follow the 3D model assets/models/module.glb (the volume knob at x 300, the display in the middle, the buttons at the right).
# usage: python3 tools/make_module_panel.py
import os, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H = 1744, 352
FONT = '/usr/share/fonts/TTF/DejaVuSans.ttf'
BOLD = '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf'
MONO = '/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf'
def font(p, s):
    try: return ImageFont.truetype(p, s)
    except OSError: return ImageFont.load_default()
img = Image.new('RGB', (W, H), (70, 71, 75))
d = ImageDraw.Draw(img)
# the brushed metal face: a soft gradient from the top, a little noise
for y in range(H):
    k = 1.0 - 0.18 * y / H
    d.line([(0, y), (W, y)], fill=(int(86 * k), int(87 * k), int(92 * k)))
random.seed(5)
px = img.load()
for _ in range(60000):
    x, y = random.randrange(W), random.randrange(H); r, g, b = px[x, y]; n = random.randint(-5, 5); px[x, y] = (r + n, g + n, b + n)
d = ImageDraw.Draw(img)
f_s, f_m, f_b = font(FONT, 19), font(BOLD, 21), font(BOLD, 24)
def label(x, y, t, f=f_s, fill=(222, 222, 226), anchor='ma'):
    d.text((x, y), t, font=f, fill=fill, anchor=anchor)
def screw(x, y):
    d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(48, 48, 52)); d.line((x - 5, y - 3, x + 5, y + 3), fill=(110, 110, 116), width=2)
# the left part: power, the MIDI socket, volume, phones
label(95, 28, 'POWER'); d.rounded_rectangle((60, 66, 150, 118), 6, fill=(30, 30, 33), outline=(15, 15, 17), width=3); d.rounded_rectangle((70, 72, 112, 112), 4, fill=(54, 54, 58))
label(100, 128, 'STANDBY'); d.ellipse((168, 82, 180, 94), fill=(66, 70, 40))
label(300, 28, 'VOLUME')
d.ellipse((248, 62, 352, 166), fill=(40, 41, 44)); d.ellipse((256, 70, 344, 158), fill=(28, 28, 31))        # the 3D knob sits here, this is its seat
d.ellipse((72, 190, 232, 330), fill=(24, 24, 27)); d.ellipse((82, 200, 222, 320), fill=(40, 40, 44)); d.ellipse((92, 210, 212, 310), fill=(14, 14, 16))
for a in range(5):                                                                                          # the five pins of the DIN socket
    import math
    ang = math.pi * (1 - a / 4.0); d.ellipse((152 + 40 * math.cos(ang) - 6, 270 - 40 * math.sin(ang) - 6, 152 + 40 * math.cos(ang) + 6, 270 - 40 * math.sin(ang) + 6), fill=(160, 160, 166))
d.rectangle((146, 207, 158, 218), fill=(70, 70, 74))
label(152, 306, 'MIDI IN', f_m)
d.ellipse((268, 236, 332, 300), fill=(24, 24, 27)); d.ellipse((276, 244, 324, 292), fill=(10, 10, 12)); label(300, 316, 'PHONES')
# the display: an amber panel with the parameters at the left and a 16 channel bar graph
d.rounded_rectangle((440, 58, 1062, 292), 10, fill=(18, 18, 20))
d.rounded_rectangle((456, 70, 1046, 280), 4, fill=(232, 168, 26))
lcd = ImageDraw.Draw(img)
for y in range(70, 280):
    k = 1.0 - 0.12 * (y - 70) / 210; lcd.line([(456, y), (1046, y)], fill=(int(236 * k), int(172 * k), int(24 * k)))
ink = (74, 44, 8)
f_l, f_lb = font(MONO, 26), font(FONT, 14)
lcd.text((470, 76), '1-1', font=f_l, fill=ink); lcd.text((640, 76), 'MIDI sound module', font=f_l, fill=ink)
lcd.text((470, 108), 'PART', font=f_lb, fill=ink); lcd.text((560, 108), 'INSTRUMENT', font=f_lb, fill=ink)
names = ['LEVEL', 'PAN', 'REVERB', 'CHORUS', 'KEY SHIFT', 'MIDI CH']
for i, t in enumerate(names):
    y = 128 + (i // 2) * 48; x = 470 + (i % 2) * 96
    lcd.text((x, y), t, font=f_lb, fill=ink)
    for c in range(3): lcd.rectangle((x + c * 24, y + 26, x + c * 24 + 18, y + 30), fill=ink)
bx0, bx1, by1 = 668, 1030, 262                                                                                # the bar graph: 16 channels
heights = [3, 8, 2, 1, 1, 1, 6, 1, 6, 16, 2, 1, 1, 1, 1, 1]
cw = (bx1 - bx0) / 16.0
for c in range(16):
    x = bx0 + c * cw
    for s in range(14):
        yy = by1 - 10 - s * 9
        on = s < heights[c]
        lcd.rectangle((x + 2, yy - 6, x + cw - 4, yy), fill=ink if on else (222, 160, 22))
    lcd.text((x + cw / 2, by1 + 2), str(c + 1), font=font(FONT, 11), fill=ink, anchor='ma')
# the middle: all / mute
d.rectangle((1112, 16, 1290, 336), fill=(30, 30, 33))
d.rounded_rectangle((1150, 70, 1240, 118), 8, fill=(56, 56, 60), outline=(14, 14, 16), width=3); d.ellipse((1257, 48, 1281, 72), fill=(240, 70, 40)); label(1210, 30, 'ALL', f_m, anchor='ma')
d.rounded_rectangle((1150, 170, 1240, 218), 8, fill=(56, 56, 60), outline=(14, 14, 16), width=3); d.ellipse((1257, 150, 1281, 174), fill=(150, 150, 154)); label(1210, 132, 'MUTE', f_m, anchor='ma')
# the right part: eight parameter buttons with arrows
d.rectangle((1296, 16, W - 14, 336), fill=(64, 65, 70))
labels = ['PART', 'INSTRUMENT', 'LEVEL', 'PAN', 'REVERB', 'CHORUS', 'KEY SHIFT', 'MIDI CH']
for i, t in enumerate(labels):
    col, row = i % 2, i // 2
    x0 = 1320 + col * 218; y0 = 46 + row * 76
    label(x0 + 90, y0 - 26, t, f_s)
    d.rounded_rectangle((x0, y0, x0 + 184, y0 + 40), 8, fill=(22, 22, 25), outline=(12, 12, 14), width=3)
    d.line((x0 + 92, y0 + 4, x0 + 92, y0 + 36), fill=(48, 48, 52), width=2)
    d.polygon([(x0 + 62, y0 + 20), (x0 + 74, y0 + 11), (x0 + 74, y0 + 29)], fill=(150, 150, 156))
    d.polygon([(x0 + 122, y0 + 20), (x0 + 110, y0 + 11), (x0 + 110, y0 + 29)], fill=(150, 150, 156))
for sx in (22, W - 22):
    for sy in (22, H - 22): screw(sx, sy)
# the edge shading
edge = Image.new('L', (W, H), 0); ed = ImageDraw.Draw(edge); ed.rectangle((0, 0, W - 1, H - 1), outline=255, width=8)
img = Image.composite(Image.new('RGB', (W, H), (22, 22, 25)), img, edge.filter(ImageFilter.GaussianBlur(7)).point(lambda v: int(v * 0.55)))
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, '..', 'assets', 'models', 'module-front.png')
img.save(out); print(out, img.size)
