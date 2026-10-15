#!/bin/sh
# Build the PC emulator (with sanitizers), run the tests, render the standard screens.
#   ./build.sh                      tests + screens
#   ./build.sh replay <args...>     run the emulator with the given arguments (see emu_main.cpp)
set -e
cd "$(dirname "$0")"
G="../.pio/libdeps/feather_sense/Adafruit GFX Library"
mkdir -p out
# 64-bit host: keep the full-width pgm_read_pointer from our stub (the library's is 32-bit)
grep -v "define pgm_read_pointer" "$G/Adafruit_GFX.cpp" > out/Adafruit_GFX_host.cpp
SRC="../src/core/xctrc.cpp ../src/core/wind.cpp ../src/core/thermal.cpp ../src/core/compass.cpp ../src/core/heading.cpp ../src/core/flight.cpp ../src/core/sim.cpp
     ../src/view/screens.cpp ../src/app/app.cpp out/Adafruit_GFX_host.cpp"
FLAGS="-std=c++17 -g -O1 -DARDUINO=100 -Wall -Wno-unused-function -Istubs -I../src -I.
       -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all"
c++ $FLAGS -I"$G" $SRC emu_main.cpp -o out/emu
c++ $FLAGS -I"$G" $SRC tests.cpp -o out/tests
if [ "$1" = "replay" ]; then shift; exec out/emu replay "$@"; fi
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
