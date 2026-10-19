"""Contact sheet of the 3D compass renders. usage: compass3d_sheet.py DIR [SHEET.png]
DIR/compass3d_NN_name.pgm -> one sheet (4 per row, 1x so every pixel is the panel's) plus a 2x png of each."""
import glob
import os
import sys
from PIL import Image, ImageDraw

d = sys.argv[1]
files = sorted(glob.glob(os.path.join(d, "compass3d_[0-9]*.pgm")))
if not files:
    sys.exit("no compass3d renders in " + d)
for p in files:
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
cols, pw, ph, gap, top = 4, 240, 400, 8, 20
rows = (len(files) + cols - 1) // cols
sheet = Image.new("L", (cols * (pw + gap) + gap, rows * (ph + top + gap) + gap), 128)
dr = ImageDraw.Draw(sheet)
for i, p in enumerate(files):
    x = gap + (i % cols) * (pw + gap)
    y = gap + (i // cols) * (ph + top + gap)
    name = os.path.basename(p)[len("compass3d_"):-4]
    dr.text((x + 2, y + 4), name.replace("_", " "), fill=255)
    sheet.paste(Image.open(p).convert("L"), (x, y + top))
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(d, "compass3d_sheet.png")
sheet.save(out)
print("wrote", out)
