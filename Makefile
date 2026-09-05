# Deliberately not hardcoded to clang++/clang: Make's built-in defaults
# (c++/cc) already resolve to the right compiler per platform (Apple clang
# on macOS, GCC on most Linux distros), and `CXX ?=`/`CC ?=` can't override
# those built-in defaults anyway (they're never "unset" as far as Make is
# concerned). Override explicitly if you want a specific one, e.g.
# `make CXX=g++ CC=gcc` or `make CXX=clang++ CC=clang`.

CXXFLAGS := -std=c++17 -O2 -Wall
CFLAGS   := -std=c11 -O2 -Wall

BUILD_DIR := build
TARGET    := imgui-test

PKG_CONFIG ?= pkg-config

# Prefer pkg-config (what most Linux distros ship for SDL2 nowadays); fall
# back to the sdl2-config script (typical on macOS/Homebrew, and on Linux
# distros that still ship it).
SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

# Same idea for libcurl: pkg-config if available, otherwise assume it's on
# the default include/library search path (true on macOS and most Linux
# distros with libcurl-dev/libcurl-devel installed).
CURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null || echo -lcurl)

INCLUDES := -Iimgui -Iimgui/backends -Iqrcodegen -Ijson -I$(BUILD_DIR) $(SDL2_CFLAGS) $(CURL_CFLAGS)
LDFLAGS  := $(SDL2_LIBS) $(CURL_LIBS)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -framework OpenGL
else
    # GLES2, not desktop GL: see the comment above the SDL_GL_SetAttribute
    # calls in main.cpp for why (Raspberry Pi's Mesa V3D driver only
    # reliably hands out GLES-capable EGL configs).
    CXXFLAGS += -DIMGUI_IMPL_OPENGL_ES2
    LDFLAGS  += -lGLESv2 -lpthread
endif

# Version shown in the app's header bar: the VERSION file (bump by hand for
# releases) plus the current commit and working-tree state, so every build
# is traceable back to exactly the commit (and any local edits) it came from.
VERSION_BASE := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
GIT_HASH     := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo nogit)
GIT_DIRTY    := $(shell test -z "$$(git status --porcelain 2>/dev/null)" || echo -dirty)
APP_VERSION  := $(VERSION_BASE)+$(GIT_HASH)$(GIT_DIRTY)

CPP_SRCS := \
    main.cpp \
    env.cpp \
    http_client.cpp \
    api_client.cpp \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_widgets.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_demo.cpp \
    imgui/backends/imgui_impl_sdl2.cpp \
    imgui/backends/imgui_impl_opengl3.cpp

C_SRCS := qrcodegen/qrcodegen.c

OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SRCS)) $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

.PHONY: all run clean linux-arm64 FORCE

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Regenerated on every `make` invocation (FORCE), but only touched on disk
# (and thus only triggers a main.o rebuild) when the version string actually
# changes - i.e. on a new commit or a newly dirtied working tree.
$(BUILD_DIR)/version.h: FORCE
	@mkdir -p $(BUILD_DIR)
	@echo '#define APP_VERSION "$(APP_VERSION)"' > $(BUILD_DIR)/version.h.tmp
	@cmp -s $(BUILD_DIR)/version.h.tmp $(BUILD_DIR)/version.h 2>/dev/null || mv $(BUILD_DIR)/version.h.tmp $(BUILD_DIR)/version.h
	@rm -f $(BUILD_DIR)/version.h.tmp

$(BUILD_DIR)/main.o: $(BUILD_DIR)/version.h

# Make has no idea that CXXFLAGS/CFLAGS changed when only the Makefile
# itself changes (e.g. after `git pull` adds a new -D flag) — it only
# compares each .o's timestamp against its .cpp/.c file. Without this, a
# stale .o built under old flags can get silently relinked, producing a
# binary that doesn't match the current source (bit us in practice: an
# object built without -DIMGUI_IMPL_OPENGL_ES2 kept the desktop-GL loader
# code path after that flag was added). Depending every object on the
# Makefile forces a full rebuild whenever it changes.
$(OBJS): Makefile

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Cross-builds a Linux/aarch64 binary (e.g. Raspberry Pi OS 64-bit) via
# Docker, dropping the result at dist/imgui-test. Requires Docker with
# buildx (Docker Desktop and OrbStack both include it).
linux-arm64:
	docker buildx build --platform linux/arm64 -f Dockerfile.linux -o type=local,dest=./dist .
