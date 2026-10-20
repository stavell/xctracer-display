# Emulator build rules, driven by build.sh (make -j<cores>). One object per source and flag set, so a source that
# several binaries share is compiled once per flag set; -MMD keeps header dependencies, so a rerun rebuilds only
# what changed. Flag sets (object dirs under out/obj/):
#   san  -O1 + address/undefined sanitizers          emu, tests
#   o2   -O2, -DGFX_COUNT, SDL cflags                sim_demo, sim_bench, sim_tests, play
#   o2n  -O2, no GFX_COUNT                           terrain_demo, sim_reach
#   ld   -O2, no ARDUINO                             loader_sim
space := $(subst ,, )
G := ../.pio/libdeps/feather_sense/Adafruit GFX Library
V := ../src/apps/vario
P := ../src/platform
T := ../src/apps/games/tetris
L := ../src/apps/launcher
S := ../src/apps/settings
M := ../src/apps/games/sim
GFXHOST := out/Adafruit_GFX_host.cpp

STORAGE := $P/storage/pack_header.cpp $P/storage/pack_proto.cpp $P/storage/pack_loader.cpp $P/storage/flash_log.cpp $P/storage/log_shell.cpp
TERRAIN := $P/terrain/crc32.cpp $P/terrain/tile_codec.cpp $P/terrain/terrain.cpp $P/terrain/relief.cpp $P/terrain/map_vectors.cpp
TETRIS := $T/tetris.cpp $T/tetris_view.cpp $T/tetris_app.cpp
SIM := $M/glider.cpp $M/height_cache.cpp $M/air.cpp $M/world.cpp $M/voxel.cpp $M/chase.cpp $M/sky3d.cpp $M/topdown.cpp $M/sim_hud.cpp $M/sim_app.cpp
LAUNCHER := $L/launcher_app.cpp $L/launcher_view.cpp
INTRO := ../src/apps/intro/intro_app.cpp
COMPASS3D := ../src/apps/compass3d/compass3d.cpp ../src/apps/compass/compass_app.cpp
SETTINGS := $S/settings_app.cpp $S/settings_view.cpp $P/settings/box_settings.cpp $P/settings/compass_cal.cpp $P/settings/cal_view.cpp
GFX := $P/util/fastmem.cpp $P/gfx/bitmap1.cpp $P/gfx/gray.cpp $P/gfx/atlas.cpp $P/gfx/font.cpp $P/gfx/shapes.cpp $P/gfx/ui.cpp $P/gfx/widgets/compass_dial.cpp $P/gfx/icons.cpp $P/ui/system_sheet.cpp
VARIO := $V/core/xctrc.cpp $V/core/wind.cpp $V/core/windekf.cpp $V/core/thermal.cpp $V/core/thermal3d.cpp $P/sensors/compass.cpp $P/sensors/ahrs.cpp $P/sensors/motion.cpp $P/power/retained.cpp $P/sensors/mount.cpp $P/sensors/sun.cpp $V/core/heading.cpp $P/sensors/heading_smoother.cpp $V/core/map_motion.cpp $V/core/flight.cpp $V/core/windprofile.cpp $V/core/home.cpp $V/core/sim.cpp $P/panel/linediff.cpp $P/panel/sharp_frame.cpp $V/core/devmatch.cpp $V/app/command_decode.cpp $V/view/screens.cpp $V/view/terrain_map.cpp $V/view/prof.cpp $V/app/app.cpp $V/core/deadreckon.cpp $V/core/flight_log.cpp

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)

COMMON := -std=c++17 -g -Wall -Istubs -I../src -I. -I"$G" -MMD -MP
SAN_F := $(COMMON) -O1 -DARDUINO=100 -Wno-unused-function -DGFX_COUNT -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
O2_F := $(COMMON) -O2 -DARDUINO=100 -Wno-unused-function -DGFX_COUNT $(SDL_CFLAGS)
O2N_F := $(COMMON) -O2 -DARDUINO=100 -Wno-unused-function
LD_F := -std=c++17 -g -Wall -I../src -I. -MMD -MP -O2

# obj dir/name for a source in a set
obj = out/obj/$(1)/$(subst /,_,$(patsubst ../%,%,$(2))).o
objs = $(foreach s,$(2),$(call obj,$(1),$(s)))

# one compile rule per (set, source)
define RULE
$(call obj,$(1),$(2)): $(2) | out/obj/$(1)
	$$(CXX) $$($(3)) -c "$$<" -o $$@
endef
define RULES
$(foreach s,$(2),$(eval $(call RULE,$(1),$(s),$(3))))
endef

LIBSRC_SAN := $(VARIO) $(GFX) $(TETRIS) $(SIM) $(INTRO) $(COMPASS3D) $(LAUNCHER) $(SETTINGS) $(TERRAIN) $(STORAGE) $(GFXHOST)
LIBSRC_O2 := $(SIM) $(INTRO) $(COMPASS3D) $(GFX) $(VARIO) $(SETTINGS) $(TETRIS) $(LAUNCHER) $(TERRAIN) $(GFXHOST)

$(eval $(call RULES,san,$(LIBSRC_SAN) emu_main.cpp tests.cpp,SAN_F))
$(eval $(call RULES,o2,$(LIBSRC_O2) sim_bench.cpp sim_tests_main.cpp sim_demo.cpp play.cpp,O2_F))
$(eval $(call RULES,o2n,$(SIM) $(GFX) $(TERRAIN) $(GFXHOST) terrain_demo.cpp sim_reach.cpp,O2N_F))
$(eval $(call RULES,ld,$(STORAGE) $V/core/flight_log.cpp $P/terrain/crc32.cpp loader_sim.cpp,LD_F))

out/obj/san out/obj/o2 out/obj/o2n out/obj/ld:
	@mkdir -p $@

# the library's Adafruit_GFX.cpp with the 32-bit pgm_read_pointer define removed (64-bit host)
$(GFXHOST): $(subst $(space),\ ,$G)/Adafruit_GFX.cpp
	@mkdir -p out
	grep -v "define pgm_read_pointer" "$<" > $@

out/emu: $(call objs,san,$(LIBSRC_SAN) emu_main.cpp)
	$(CXX) $(SAN_F) $^ -o $@
out/tests: $(call objs,san,$(LIBSRC_SAN) tests.cpp)
	$(CXX) $(SAN_F) $^ -o $@
out/sim_demo: $(call objs,o2,$(SIM) $(GFX) $(TERRAIN) $(GFXHOST) sim_demo.cpp)
	$(CXX) $(O2_F) $^ -o $@
out/sim_bench: $(call objs,o2,$(LIBSRC_O2) sim_bench.cpp)
	$(CXX) $(O2_F) $^ -o $@
out/sim_tests: $(call objs,o2,$(LIBSRC_O2) sim_tests_main.cpp)
	$(CXX) $(O2_F) $^ -o $@
out/play: $(call objs,o2,$(LIBSRC_O2) play.cpp)
	$(CXX) $(O2_F) $^ $(SDL_LIBS) -o $@
out/terrain_demo: $(call objs,o2n,$(GFX) $(TERRAIN) $(GFXHOST) terrain_demo.cpp)
	$(CXX) $(O2N_F) $^ -o $@
out/sim_reach: $(call objs,o2n,$(SIM) $(GFX) $(TERRAIN) $(GFXHOST) sim_reach.cpp)
	$(CXX) $(O2N_F) $^ -o $@
out/loader_sim: $(call objs,ld,$(STORAGE) $V/core/flight_log.cpp $P/terrain/crc32.cpp loader_sim.cpp)
	$(CXX) $(LD_F) $^ -o $@

-include $(wildcard out/obj/*/*.d)
