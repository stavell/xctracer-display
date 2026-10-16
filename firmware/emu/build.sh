#!/bin/sh
# Build the PC emulator (with sanitizers), run the tests, render the standard screens.
#   ./build.sh                      tests + screens
#   ./build.sh replay <args...>     run the emulator with the given arguments (see emu_main.cpp)
#   ./build.sh count [outdir] [rot]  per-section draw work of the live page (pixel/span call counts)
#   ./build.sh wind <lines.txt>     EKF vs circle-fit wind over a real flight (see emu_main.cpp)
#   ./build.sh soak [lines.txt]     long session through the App, about a minute (see emu_main.cpp)
set -e
cd "$(dirname "$0")"
G="../.pio/libdeps/feather_sense/Adafruit GFX Library"
mkdir -p out
# 64-bit host: keep the full-width pgm_read_pointer from our stub (the library's is 32-bit)
grep -v "define pgm_read_pointer" "$G/Adafruit_GFX.cpp" > out/Adafruit_GFX_host.cpp
SRC="../src/core/xctrc.cpp ../src/core/wind.cpp ../src/core/windekf.cpp ../src/core/thermal.cpp ../src/core/compass.cpp ../src/core/ahrs.cpp ../src/core/mount.cpp ../src/core/heading.cpp ../src/core/heading_smoother.cpp ../src/core/map_motion.cpp ../src/core/flight.cpp ../src/core/windprofile.cpp ../src/core/home.cpp ../src/core/sim.cpp ../src/core/linediff.cpp ../src/core/sharp_frame.cpp ../src/core/devmatch.cpp ../src/gfx/bitmap1.cpp ../src/gfx/gray.cpp ../src/gfx/atlas.cpp ../src/gfx/font.cpp ../src/gfx/shapes.cpp ../src/app/command_decode.cpp
     ../src/view/screens.cpp ../src/view/prof.cpp ../src/app/app.cpp out/Adafruit_GFX_host.cpp"
FLAGS="-std=c++17 -g -O1 -DARDUINO=100 -Wall -Wno-unused-function -DGFX_COUNT -Istubs -I../src -I.
       -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all"
c++ $FLAGS -I"$G" $SRC emu_main.cpp -o out/emu
c++ $FLAGS -I"$G" $SRC tests.cpp -o out/tests
if [ "$1" = "replay" ]; then shift; exec out/emu replay "$@"; fi
if [ "$1" = "count" ]; then shift; exec out/emu count "${1:-out}" "${2:-1}"; fi
if [ "$1" = "wind" ]; then shift; exec out/emu wind "$@"; fi
if [ "$1" = "soak" ]; then shift; exec out/emu soak "$@"; fi
out/tests
out/emu shots out ${ROT:-1}
../../.venv/bin/python - <<'PY'
from PIL import Image
import glob, os
for p in glob.glob("out/*.pgm"):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p[:-4] + ".png")
PY
echo "screens: $(ls out/*.png | tr '\n' ' ')"
