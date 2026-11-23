# Draws the front panel of the second MIDI sound module, the black one that stands for files written for the MT-32 (own artwork, no brand names or logos):
# assets/models/module-mt-front.png, 1600 x 236. The positions follow the 3D model assets/models/midi-module-mt.obj (the display at the left, the select knob at the right).
# usage: python3 tools/make_module_panel_mt.py
import os, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H = 1600, 236
FONT = '/usr/share/fonts/TTF/DejaVuSans.ttf'; BOLD = '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf'; MONO = '/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf'
def font(p, s):
    try: return ImageFont.truetype(p, s)
    except OSError: return ImageFont.load_default()
img = Image.new('RGB', (W, H), (40, 40, 44)); d = ImageDraw.Draw(img)
for y in range(H):
    k = 1.0 - 0.22 * y / H; d.line([(0, y), (W, y)], fill=(int(52 * k), int(52 * k), int(57 * k)))
random.seed(11); px = img.load()
for _ in range(40000):
    x, y = random.randrange(W), random.randrange(H); r, g, b = px[x, y]; n = random.randint(-4, 4); px[x, y] = (r + n, g + n, b + n)
d = ImageDraw.Draw(img)
f_s, f_m, f_b = font(FONT, 15), font(BOLD, 17), font(BOLD, 21)
LAB = (205, 205, 210); BLUE = (120, 160, 225)
def label(x, y, t, f=f_s, fill=LAB): d.text((x, y), t, font=f, fill=fill, anchor='ma')
sx = W / 1525.0                                                    # the positions are taken from the proportions of the model (the front is 305 x 45)
# the display window at the left: a recessed bezel, a pale backlit LCD with two lines
d.rectangle((int(130 * sx), 4, int(770 * sx), H - 20), fill=(24, 24, 27)); d.rectangle((int(130 * sx) + 6, 10, int(770 * sx) - 6, H - 26), fill=(32, 32, 36))
label(int(450 * sx), 28, 'MIDI SOUND MODULE', f_b, (230, 230, 235))
lx0, lx1, ly0, ly1 = int(205 * sx), int(620 * sx), int(85 * sx), int(140 * sx) + 6
d.rounded_rectangle((lx0, ly0, lx1, ly1), 4, fill=(24, 62, 10))
for y in range(ly0, ly1):                                             # the LCD: a dark green base; the player lights the character cells and writes the title of the MIDI file into them at run time
    k = 1.0 - 0.10 * (y - ly0) / (ly1 - ly0); d.line([(lx0 + 2, y), (lx1 - 2, y)], fill=(int(30 * k), int(78 * k), int(12 * k)))
d.ellipse((int(690 * sx) - 5, 108, int(690 * sx) + 5, 118), fill=(64, 90, 56)); label(int(690 * sx), 78, 'MIDI', f_s)
# the buttons: five parts and the rhythm part, then group / sound and level / master
def button(x0, y0, x1, y1, text, tcol=LAB, above=None, ac=BLUE):
    d.rounded_rectangle((x0 - 1, y0 - 1, x1 + 1, y1 + 1), 4, fill=(10, 10, 12)); d.rounded_rectangle((x0, y0, x1, y1), 4, fill=(22, 22, 26))
    d.line((x0 + 3, y0 + 2, x1 - 3, y0 + 2), fill=(58, 58, 64), width=2)
    if text: d.text(((x0 + x1) // 2, y1 + 3), text, font=f_s, fill=tcol, anchor='ma')
    if above: d.text(((x0 + x1) // 2, y0 - 22), above, font=f_s, fill=ac, anchor='ma')
bw = int(46 * sx)
for i, t in enumerate(['1', '2', '3']): button(int(862 * sx) + i * int(72 * sx), 50, int(862 * sx) + i * int(72 * sx) + bw, 88, t)
for i, t in enumerate(['4', '5', 'RHYTHM']): button(int(862 * sx) + i * int(72 * sx), 120, int(862 * sx) + i * int(72 * sx) + bw, 158, t)
d.line((int(868 * sx), 38, int(1040 * sx), 38), fill=LAB, width=1); label(int(955 * sx), 24, 'PART', f_s)
button(int(1097 * sx), 55, int(1185 * sx), 90, '', above='GROUP'); button(int(1097 * sx), 122, int(1185 * sx), 157, '', above='SOUND', ac=BLUE)
button(int(1215 * sx), 55, int(1300 * sx), 90, '', above='LEVEL'); button(int(1215 * sx), 122, int(1300 * sx), 157, '', above='MASTER')
# the knob: its seat with the dots around it (the 3D knob sits on it)
kx, ky, kr = int(1410 * sx), 112, 46
d.ellipse((kx - kr, ky - kr, kx + kr, ky + kr), fill=(18, 18, 21))
import math
for a in range(24):
    ang = math.radians(-135 + a * (270.0 / 23)); x = kx + (kr + 12) * math.cos(ang - math.pi / 2); y = ky + (kr + 12) * math.sin(ang - math.pi / 2); d.ellipse((x - 2, y - 2, x + 2, y + 2), fill=(200, 200, 205))
label(kx, 24, 'SELECT', f_s)
# the edge shading
edge = Image.new('L', (W, H), 0); ImageDraw.Draw(edge).rectangle((0, 0, W - 1, H - 1), outline=255, width=7)
img = Image.composite(Image.new('RGB', (W, H), (14, 14, 16)), img, edge.filter(ImageFilter.GaussianBlur(6)).point(lambda v: int(v * 0.5)))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'models', 'module-mt-front.png')
img.save(out); print(out, img.size)
