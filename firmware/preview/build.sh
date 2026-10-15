#!/bin/sh
# Build the UI preview and render every scenario to PNG in ./out. Needs Pillow (see ../../.venv).
set -e
cd "$(dirname "$0")"
G="../.pio/libdeps/feather_sense/Adafruit GFX Library"
mkdir -p out
# 64-bit host: keep the full-width pgm_read_pointer from our stub (the library's is 32-bit)
grep -v "define pgm_read_pointer" "$G/Adafruit_GFX.cpp" > out/Adafruit_GFX_host.cpp
c++ -std=c++17 -O1 -DARDUINO=100 -Istubs -I"$G" -I../src -I. \
  preview_main.cpp ../src/ui.cpp out/Adafruit_GFX_host.cpp -o out/preview
ROT=${ROT:-1}
for s in live nowind tail head big nofix status list; do
  out/preview $s out/$s.pgm $ROT
  ../../.venv/bin/python -c "
from PIL import Image
im = Image.open('out/$s.pgm'); im = im.resize((im.width*2, im.height*2), Image.NEAREST)
im.save('out/$s.png')"
done
echo rendered: $(ls out/*.png)
