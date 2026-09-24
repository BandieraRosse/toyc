WIN_CC ?= x86_64-w64-mingw32-gcc
AR ?= x86_64-w64-mingw32-ar
WIN_AR ?= x86_64-w64-mingw32-ar
# Windows GNU Make otherwise defaults recipes to cmd.exe.  The rules below
# intentionally use POSIX commands, so run them through the fixed MSYS2 shell.
MSYS2_ROOT ?= C:/msys64
SHELL := $(MSYS2_ROOT)/usr/bin/sh.exe
# A shell launched by native Windows GNU Make does not inherit MSYS2's
# converted PATH reliably; export the two required MSYS2 namespaces explicitly.
export PATH := /mingw64/bin:/usr/bin
# Keep Windows objects, executable and package completely separate from the
# Linux/self-hosting build.  Override explicitly only for a disposable build.
BUILD ?= build-windows
WINDOWS_DEPS ?= $(CURDIR)/.windows-deps
SDL_PREFIX ?= $(WINDOWS_DEPS)/x86_64-w64-mingw32
PYTHON ?= python3

CPPFLAGS := -DTOYC_WINDOWS \
            -I$(CURDIR)/windows/include -Iinclude \
            -idirafter include/posix -idirafter include/tlibc \
            -Irasterfall/include -Irasterfall/lib -Igpu/include \
            -I$(SDL_PREFIX)/include -I$(SDL_PREFIX)/include/SDL2 \
            -include windows/include/windows_stdlib.h
CFLAGS := -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
          -MMD -MP
# The aggregate logic-test keeps large gameplay/session fixtures on its call stack.
# Reserve 16 MiB for the Windows main thread; pages are committed on demand.
LDFLAGS := -static -static-libgcc -mwindows -municode -Wl,--gc-sections -Wl,--stack,16777216
LDLIBS := -L$(SDL_PREFIX)/lib -lSDL2 -lws2_32 -lwinmm \
          -lgdi32 -limm32 -lole32 -loleaut32 -luuid -lversion -lsetupapi \
          -lshell32

GAME_SRCS := rasterfall/src/rasterfall.c \
             rasterfall/src/rf_gpu_scene_native.c \
             rasterfall/src/rf_gpu_scene_local.c \
             rasterfall/src/rf_gpu_scene_world.c \
             rasterfall/src/rf_gpu_scene_world_gpu.c \
             rasterfall/src/rf_gpu_scene_identity.c \
             rasterfall/src/rf_gpu_scene_frame.c \
             rasterfall/src/rf_gpu_scene_extract.c \
             rasterfall/src/rf_game_runtime.c \
             rasterfall/src/rasterfall_gui.c \
             rasterfall/src/rasterfall_app.c \
             rasterfall/src/rf_application_projection.c \
             rasterfall/src/rasterfall_character.c \
             rasterfall/src/rasterfall_roster.c \
             rasterfall/src/rasterfall_prop.c \
             rasterfall/src/rasterfall_audio.c \
             rasterfall/src/rasterfall_effects.c \
             rasterfall/src/rasterfall_hud.c \
             rasterfall/src/rasterfall_map.c \
             rasterfall/src/rasterfall_model.c \
             rasterfall/src/rasterfall_action.c \
             rasterfall/src/rasterfall_humanoid_basis.c \
             rasterfall/src/rasterfall_humanoid_retarget.c \
             rasterfall/src/rasterfall_ai.c \
             rasterfall/src/rasterfall_net.c \
             rasterfall/src/rasterfall_net_transport.c \
             rasterfall/src/rasterfall_net_discovery.c \
             rasterfall/src/rasterfall_perf.c \
             rasterfall/src/rasterfall_calibration.c \
             rasterfall/src/rasterfall_console.c \
             rasterfall/src/rasterfall_options.c \
             rasterfall/src/rasterfall_render.c \
             rasterfall/src/rasterfall_world_light.c \
             rasterfall/src/render/rasterfall_render_frontend.c \
             rasterfall/src/render/rasterfall_render_resources.c \
             rasterfall/src/rasterfall_session.c \
             rasterfall/src/rasterfall_world_content.c \
             rasterfall/src/rasterfall_sky.c \
             rasterfall/src/rasterfall_viewmodel.c
GAME_SRCS += gpu/src/rf_gpu_raster_pack.c
GAME_SRCS += rasterfall/src/rf_viewmodel_contract.c
GAME_SRCS += gpu/src/rf_gpu_raster_bin.c gpu/src/rf_gpu_vulkan_backend.c \
             gpu/src/rf_gpu_mixed_executor.c gpu/src/rf_gpu_resource_cache.c
ANIMATION_SRCS := app/linux/glb_inspect.c rasterfall/src/rasterfall_vmd.c
GAME_LIB_SRCS := rasterfall/lib/game.c rasterfall/lib/map.c rasterfall/lib/sfx.c \
                 rasterfall/lib/rasterfall_map_parser.c \
                 rasterfall/lib/rasterfall_map_runtime.c \
                 rasterfall/lib/rasterfall_map_components.c
PLATFORM_SRCS := rasterfall/src/rf_core_filesystem.c rasterfall/src/rf_gpu.c rasterfall/src/rf_core_host.c gpu/src/rf_gpu_raster_cpu_ref.c rasterfall/src/rf_game_lifecycle.c windows/src/runtime.c windows/src/socket_winsock.c windows/src/window_sdl.c \
                 windows/src/audio_sdl.c windows/src/pthread_win32.c windows/src/winmain.c \
                 windows/src/env.c windows/src/string.c windows/src/platform.c
LIB_SRCS := lib/linux/assets.c lib/linux/graphics/fb_draw.c lib/linux/graphics/fb_font.c \
            lib/linux/graphics/renderer.c lib/linux/input/input.c lib/linux/net/dns.c \
            lib/portable/math.c
SRCS := $(GAME_SRCS) $(ANIMATION_SRCS) $(GAME_LIB_SRCS) $(PLATFORM_SRCS) $(LIB_SRCS)
OBJS := $(patsubst %.c,$(BUILD)/win/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TARGET := $(BUILD)/rasterfall.exe
PACKAGE_DIR := $(BUILD)/rasterfall-windows
PACKAGE := $(BUILD)/rasterfall-windows.zip

# Userland source sets are explicit.  The portable set is shared with Linux;
# the platform set is the only place for Windows replacements.  Rasterfall's
# separate PLATFORM_SRCS above is intentionally not part of this contract.
PORTABLE_APP_SRCS := $(shell find app/portable -name '*.c' | LANG=C sort)
WINDOWS_NATIVE_APP_SRCS := $(shell find app/windows -name '*.c' | LANG=C sort)
WINDOWS_APP_SRCS := $(PORTABLE_APP_SRCS) $(WINDOWS_NATIVE_APP_SRCS)
PORTABLE_APP_NAMES := $(sort $(basename $(notdir $(WINDOWS_APP_SRCS))))
ifneq ($(words $(WINDOWS_APP_SRCS)),$(words $(PORTABLE_APP_NAMES)))
$(error duplicate Windows app basename detected; keep app source names unique)
endif
WINDOWS_PORTABLE_LIB_SRCS := $(shell find lib/portable -name '*.c' | LANG=C sort)
WINDOWS_NATIVE_LIB_SRCS := $(shell find lib/windows -name '*.c' | LANG=C sort)
WINDOWS_REPLACEMENT_LIB_SRCS := $(WINDOWS_NATIVE_LIB_SRCS)
WINDOWS_LIB_SRCS := $(WINDOWS_PORTABLE_LIB_SRCS) $(WINDOWS_REPLACEMENT_LIB_SRCS)
WINDOWS_LIB_OBJS := $(addprefix $(BUILD)/windows/,$(WINDOWS_LIB_SRCS:.c=.o))
WINDOWS_LIB := $(BUILD)/windows/toyc-windows.a
WINDOWS_APP_OBJS := $(addprefix $(BUILD)/windows/,$(WINDOWS_APP_SRCS:.c=.o))
WINDOWS_APP_TARGETS := $(addprefix $(BUILD)/windows/,$(addsuffix .exe,$(PORTABLE_APP_NAMES)))
APP_CPPFLAGS := -DTOYC_WINDOWS -I$(CURDIR)/windows/include -Iinclude \
                -idirafter include/posix -idirafter include/tlibc
APP_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -MMD -MP
APP_LDFLAGS := -static -static-libgcc -mconsole -municode -Wl,--gc-sections
WINDOWS_APP_SHIM_SRC := windows/src/userland_winmain.c
WINDOWS_APP_SHIM_OBJ := $(BUILD)/windows/userland_winmain.o

.PHONY: all app package clean platform-sources pe-audit $(addprefix win-app-,$(PORTABLE_APP_NAMES))

# Windows 构建与 Linux 构建共用头文件和玩法/渲染源码；无条件重建对象，
# 防止依赖文件缺失或切换工作区状态时复用与头文件不一致的旧对象。
rasterfall-rebuild:

all: $(TARGET)

app: $(WINDOWS_APP_TARGETS)

$(foreach name,$(PORTABLE_APP_NAMES),$(eval win-app-$(name): $(BUILD)/windows/$(name).exe))

# Print the selected provider set for review and CI logs.  The common I/O
# provider is intentionally one replacement source on Windows; CP2 will
# change its implementation without changing this source-selection contract.
platform-sources:
	@printf '%s\n' 'Windows userland source selection:'
	@printf '  portable: %s\n' '$(WINDOWS_PORTABLE_LIB_SRCS)'
	@printf '  replacement/platform: %s\n' '$(WINDOWS_REPLACEMENT_LIB_SRCS)'
	@printf '  portable apps: %s\n' '$(PORTABLE_APP_SRCS)'
	@printf '  Windows-native apps: %s\n' '$(WINDOWS_NATIVE_APP_SRCS)'
	@printf '%s\n' '  provider common file/path/dir contract: lib/windows/core/io.c (single Windows provider)'
	@test "$(words $(WINDOWS_LIB_SRCS))" -eq "$(words $(sort $(WINDOWS_LIB_SRCS)))"
	@test "$(words $(WINDOWS_APP_SRCS))" -eq "$(words $(sort $(WINDOWS_APP_SRCS)))"

pe-audit: $(WINDOWS_APP_TARGETS)
	@command -v x86_64-w64-mingw32-objdump >/dev/null
	@x86_64-w64-mingw32-objdump -p $(BUILD)/windows/cat.exe | grep -E 'CreateFileW|ReadFile|WriteFile|CloseHandle|MultiByteToWideChar|WideCharToMultiByte'
	@x86_64-w64-mingw32-objdump -p $(BUILD)/windows/grep.exe | grep -E 'FindFirstFileW|FindNextFileW|FindClose|GetFileInformationByHandle|CreateDirectoryW|DeleteFileW|RemoveDirectoryW|MoveFileExW|GetCurrentDirectoryW|SetCurrentDirectoryW|SetFilePointerEx'

$(WINDOWS_LIB): $(WINDOWS_LIB_OBJS) | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-AR  %s\n' "$@"
	$(WIN_AR) rcs $@ $^

define WINDOWS_APP_rule

$(BUILD)/windows/$(1:.c=.o): $(1) windows/Makefile | $(BUILD)
	@mkdir -p "$$(dir $$@)"
	@printf '  WIN-CC  %s\n' "$$<"
	$$(WIN_CC) $$(APP_CPPFLAGS) $$(APP_CFLAGS) -c $$< -o $$@

$(BUILD)/windows/$(notdir $(basename $(1))).exe: $(BUILD)/windows/$(1:.c=.o) $(WINDOWS_LIB) $(WINDOWS_APP_SHIM_OBJ)
	@printf '  WIN-LD  %s\n' "$$@"
	$$(WIN_CC) $$(APP_LDFLAGS) -o $$@ $$< $(WINDOWS_APP_SHIM_OBJ) $(WINDOWS_LIB)
endef
$(foreach src,$(WINDOWS_APP_SRCS),$(eval $(call WINDOWS_APP_rule,$(src))))

$(BUILD)/windows/%.o: %.c windows/Makefile | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s\n' "$<"
	$(WIN_CC) $(APP_CPPFLAGS) $(APP_CFLAGS) -c $< -o $@

$(WINDOWS_APP_SHIM_OBJ): $(WINDOWS_APP_SHIM_SRC) windows/Makefile | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s\n' "$<"
	$(WIN_CC) $(APP_CPPFLAGS) $(APP_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) | $(BUILD)
	@printf '  WIN-LD  %s\n' "$@"
	$(WIN_CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Hosted resource lifecycle integration, excluded from the player package.
RESOURCE_TEST_OBJS := $(filter-out $(BUILD)/win/rasterfall/src/rasterfall.o $(BUILD)/win/windows/src/winmain.o,$(OBJS))
$(BUILD)/rasterfall-resource-test.exe: $(BUILD)/win/tools/rasterfall_resource_test.o $(RESOURCE_TEST_OBJS)
	$(WIN_CC) -static -static-libgcc -Wl,--gc-sections -Wl,--stack,16777216 -o $@ $^ $(LDLIBS)
-include $(BUILD)/win/tools/rasterfall_resource_test.d

.PHONY: gpu-resource-cache-test
.PHONY: gpu-mixed-executor-test
gpu-mixed-executor-test: $(BUILD)/rasterfall-gpu-mixed-test.exe
$(BUILD)/rasterfall-gpu-mixed-test.exe: $(BUILD)/win/tools/rasterfall_gpu_mixed_test.o $(RESOURCE_TEST_OBJS)
	$(WIN_CC) -static -static-libgcc -Wl,--gc-sections -Wl,--stack,16777216 -o $@ $^ $(LDLIBS)
-include $(BUILD)/win/tools/rasterfall_gpu_mixed_test.d $(BUILD)/win/gpu/src/rf_gpu_mixed_executor.d

gpu-resource-cache-test: $(BUILD)/rasterfall-gpu-cache-test.exe
$(BUILD)/rasterfall-gpu-cache-test.exe: $(BUILD)/win/tools/rasterfall_gpu_cache_test.o $(RESOURCE_TEST_OBJS)
	$(WIN_CC) -static -static-libgcc -Wl,--gc-sections -Wl,--stack,16777216 -o $@ $^ $(LDLIBS)
-include $(BUILD)/win/tools/rasterfall_gpu_cache_test.d $(BUILD)/win/gpu/src/rf_gpu_resource_cache.d

.PHONY: gpu-scene-identity-test
gpu-scene-identity-test: $(BUILD)/rf-gpu-scene-identity-test.exe
$(BUILD)/rf-gpu-scene-identity-test.exe: tools/rf_gpu_scene_identity_test.c rasterfall/src/rf_gpu_scene_identity.c rasterfall/include/rf_gpu_scene_identity.h | $(BUILD)
	$(WIN_CC) -std=c11 -O2 -Wall -Wextra -Werror -I windows/include -I include -I include/tlibc -I rasterfall/include $< rasterfall/src/rf_gpu_scene_identity.c -o $@

.PHONY: gpu-scene-frame-test
gpu-scene-frame-test: $(BUILD)/rf-gpu-scene-frame-test.exe
$(BUILD)/rf-gpu-scene-frame-test.exe: tools/rf_gpu_scene_frame_test.c rasterfall/src/rf_gpu_scene_frame.c rasterfall/src/rf_gpu_scene_extract.c rasterfall/src/rf_gpu_scene_identity.c rasterfall/include/rf_gpu_scene_frame.h rasterfall/include/rf_gpu_scene_extract.h rasterfall/include/rf_gpu_scene_identity.h | $(BUILD)
	$(WIN_CC) -std=c11 -O2 -Wall -Wextra -Werror -I windows/include -I include -I include/tlibc -I rasterfall/include $< rasterfall/src/rf_gpu_scene_frame.c rasterfall/src/rf_gpu_scene_extract.c rasterfall/src/rf_gpu_scene_identity.c -o $@

# GPU-GRAPHICS hosted diagnostic: independent of SDL and player package/assets.
GPU_GRAPHICS_SRCS := gpu/src/rf_gpu_graphics_test.c gpu/src/rf_gpu_vulkan_backend.c \
    gpu/src/rf_gpu_raster_pack.c gpu/src/rf_gpu_raster_bin.c rasterfall/src/rf_gpu.c
GPU_GRAPHICS_DEPS := $(wildcard gpu/include/*.h gpu/src/*spirv.inc) \
    gpu/src/rf_gpu_vulkan_graphics.inc gpu/shaders/graphics_v0.vert gpu/shaders/graphics_v0.frag \
    gpu/shaders/graphics_compat.vert gpu/shaders/graphics_compat.frag gpu/shaders/graphics_bridge.comp \
    $(wildcard rasterfall/include/rf_gpu*.h) include/toy_renderer.h
.PHONY: gpu-graphics-test
gpu-graphics-test: $(BUILD)/rf-gpu-graphics-test.exe
.PHONY: gpu-raster-test
gpu-raster-test: $(BUILD)/rf-gpu-raster-test.exe
$(BUILD)/rf-gpu-raster-test.exe: gpu/src/rf_gpu_raster_test.c $(GPU_GRAPHICS_SRCS) $(GPU_GRAPHICS_DEPS) | $(BUILD)
	$(WIN_CC) -std=c11 -O2 -Wall -Wextra -Werror -I gpu/include -I windows/include \
	    -I include -I include/tlibc -I rasterfall/include gpu/src/rf_gpu_raster_test.c \
	    $(filter-out gpu/src/rf_gpu_graphics_test.c,$(GPU_GRAPHICS_SRCS)) -o $@
$(BUILD)/rf-gpu-graphics-test.exe: $(GPU_GRAPHICS_SRCS) $(GPU_GRAPHICS_DEPS) | $(BUILD)
	$(WIN_CC) -std=c11 -O2 -Wall -Wextra -Werror -I gpu/include -I windows/include \
	    -I include -I include/tlibc -I rasterfall/include $(GPU_GRAPHICS_SRCS) -o $@

package: $(TARGET)
	@rm -rf "$(PACKAGE_DIR)"
	@mkdir -p "$(PACKAGE_DIR)/rasterfall"
	@cp "$(TARGET)" "$(PACKAGE_DIR)/rasterfall.exe"
	@cp -R rasterfall/assets "$(PACKAGE_DIR)/rasterfall/assets"
	@if [ -d rasterfall/private-assets ]; then cp -R rasterfall/private-assets "$(PACKAGE_DIR)/rasterfall/private-assets"; fi
	@rm -f "$(PACKAGE)"
	@cd "$(PACKAGE_DIR)" && $(PYTHON) -m zipfile -c "$(abspath $(PACKAGE))" rasterfall.exe rasterfall
	@printf '  WIN-ZIP %s\n' "$(PACKAGE)"

$(BUILD)/win/app/linux/glb_inspect.o: app/linux/glb_inspect.c windows/Makefile rasterfall-rebuild | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s (library)\n' "$<"
	$(WIN_CC) $(CPPFLAGS) $(CFLAGS) -DRASTERFALL_GLB_LIBRARY=1 -c $< -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/win/%.o: %.c windows/Makefile rasterfall-rebuild | $(BUILD)
	@mkdir -p "$(dir $@)"
	@printf '  WIN-CC  %s\n' "$<"
	$(WIN_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD)/win
	rm -rf $(PACKAGE_DIR)
	rm -f $(TARGET) $(PACKAGE)
