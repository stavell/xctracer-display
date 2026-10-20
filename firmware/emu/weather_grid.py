"""The weather sleep screen with the 6 h grid: weather_grid.py DIR OUT.png. Three renders side by side: clock known (wall-clock hours),
a deep fall at a sub-zero temperature, nothing known (grid every 6 h back from now, dashes)."""
import sys
from PIL import Image
d, out = sys.argv[1], sys.argv[2]
names = ["sleep_clock", "sleep_station_storm", "sleep_clock_unknown"]
ims = [Image.open("%s/%s.pgm" % (d, n)).convert("L") for n in names]
sheet = Image.new("L", (len(ims) * 248 + 8, 408), 128)
for i, im in enumerate(ims):
    sheet.paste(im, (8 + i * 248, 4))
sheet.save(out)
print("wrote", out)
