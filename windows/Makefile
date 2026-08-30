WIN_CC ?= x86_64-w64-mingw32-gcc
AR ?= x86_64-w64-mingw32-ar
BUILD ?= build
WINDOWS_DEPS ?= $(CURDIR)/.windows-deps
SDL_PREFIX ?= $(WINDOWS_DEPS)/x86_64-w64-mingw32

CPPFLAGS := -DTOYC_WINDOWS \
            -I$(CURDIR)/windows/include -Iinclude \
            -idirafter include/posix -idirafter include/tlibc \
            -Irasterfall/include -Irasterfall/lib \
            -I$(SDL_PREFIX)/include -I$(SDL_PREFIX)/include/SDL2 \
            -include windows/include/windows_stdlib.h
CFLAGS := -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
          -MMD -MP
LDFLAGS := -static -static-libgcc -mwindows -municode -Wl,--gc-sections
LDLIBS := -L$(SDL_PREFIX)/lib -lSDL2 -lws2_32 -lwinmm \
          -lgdi32 -limm32 -lole32 -loleaut32 -luuid -lversion -lsetupapi \
          -lshell32

GAME_SRCS := rasterfall/src/rasterfall.c \
             rasterfall/src/rasterfall_character.c \
             rasterfall/src/rasterfall_audio.c \
             rasterfall/src/rasterfall_effects.c \
             rasterfall/src/rasterfall_hud.c \
             rasterfall/src/rasterfall_map.c \
             rasterfall/src/rasterfall_model.c \
             rasterfall/src/rasterfall_humanoid_basis.c \
             rasterfall/src/rasterfall_humanoid_retarget.c \
             rasterfall/src/rasterfall_ai.c \
             rasterfall/src/rasterfall_net.c \
             rasterfall/src/rasterfall_net_discovery.c \
             rasterfall/src/rasterfall_perf.c \
             rasterfall/src/rasterfall_calibration.c \
             rasterfall/src/rasterfall_console.c \
             rasterfall/src/rasterfall_render.c \
             rasterfall/src/rasterfall_session.c \
             rasterfall/src/rasterfall_sky.c \
             rasterfall/src/rasterfall_viewmodel.c
ANIMATION_SRCS := app/glb_inspect.c rasterfall/src/rasterfall_vmd.c
GAME_LIB_SRCS := rasterfall/lib/game.c rasterfall/lib/map.c rasterfall/lib/sfx.c
PLATFORM_SRCS := windows/src/runtime.c windows/src/socket_winsock.c windows/src/window_sdl.c \
                 windows/src/audio_sdl.c windows/src/pthread_win32.c windows/src/winmain.c \
                 windows/src/env.c windows/src/string.c windows/src/platform.c
LIB_SRCS := lib/assets.c lib/graphics/fb_draw.c lib/graphics/fb_font.c \
            lib/graphics/renderer.c lib/input/input.c lib/net/dns.c \
            lib/math/math.c
SRCS := $(GAME_SRCS) $(ANIMATION_SRCS) $(GAME_LIB_SRCS) $(PLATFORM_SRCS) $(LIB_SRCS)
OBJS := $(patsubst %.c,$(BUILD)/win/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TARGET := $(BUILD)/rasterfall.exe
PACKAGE_DIR := $(BUILD)/rasterfall-windows
PACKAGE := $(BUILD)/rasterfall-windows.zip

.PHONY: all package clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD)
	@printf '  WIN-LD  %s\n' "$@"
	$(WIN_CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

package: $(TARGET)
	@rm -rf "$(PACKAGE_DIR)"
	@mkdir -p "$(PACKAGE_DIR)/rasterfall"
	@cp "$(TARGET)" "$(PACKAGE_DIR)/rasterfall.exe"
	@cp -R rasterfall/assets "$(PACKAGE_DIR)/rasterfall/assets"
	@cp -R rasterfall/private-assets "$(PACKAGE_DIR)/rasterfall/private-assets"
	@rm -f "$(PACKAGE)"
	@cd "$(PACKAGE_DIR)" && python3 -m zipfile -c "$(abspath $(PACKAGE))" rasterfall.exe rasterfall
	@printf '  WIN-ZIP %s\n' "$(PACKAGE)"

$(BUILD)/win/app/glb_inspect.o: app/glb_inspect.c windows/Makefile | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s (library)\n' "$<"
	$(WIN_CC) $(CPPFLAGS) $(CFLAGS) -DRASTERFALL_GLB_LIBRARY=1 -c $< -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/win/%.o: %.c windows/Makefile | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s\n' "$<"
	$(WIN_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD)/win
	rm -rf $(PACKAGE_DIR)
	rm -f $(TARGET) $(PACKAGE)
