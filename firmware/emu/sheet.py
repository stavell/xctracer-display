"""PNG conversion of the sim renders and the thermal-style contact sheet. usage: sheet.py DIR [SHEET.png]
DIR/sim_*.pgm -> .png at 2x; the sheet puts the thermal scenes side by side: rows approach / inside the thermal,
columns clouds only (assist off) / columns / bubbles / both (1x, so every pixel is the panel's)."""
import glob
import os
import sys
from PIL import Image, ImageDraw

d = sys.argv[1]
for p in glob.glob(os.path.join(d, "sim_*.pgm")):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")

cols = [("clouds", "assist off: clouds only"), ("columns", "columns"), ("bubbles", "bubbles"), ("both", "both")]
rows = [("approach", "approaching, 1.6 km"), ("inside", "circling inside")]
files = [[os.path.join(d, "sim_thermal_%s_%s.pgm" % (r, c)) for c, _ in cols] for r, _ in rows]
if all(os.path.exists(f) for fr in files for f in fr):
    pw, ph, gap, top = 240, 400, 8, 22
    sheet = Image.new("L", (len(cols) * (pw + gap) + gap, len(rows) * (ph + top + gap) + gap), 128)
    dr = ImageDraw.Draw(sheet)
    for ri, (rk, rl) in enumerate(rows):
        for ci, (ck, cl) in enumerate(cols):
            x = gap + ci * (pw + gap)
            y = gap + ri * (ph + top + gap)
            dr.text((x + 2, y + 4), "%s - %s" % (rl, cl), fill=255)
            sheet.paste(Image.open(files[ri][ci]).convert("L"), (x, y + top))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(d, "sim_thermal_styles.png")
    sheet.save(out)
    print("wrote", out)
