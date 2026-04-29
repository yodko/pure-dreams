RACK_DIR ?= ../Rack

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

PROJECTM_PREFIX ?= /opt/homebrew/Cellar/projectm/3.1.12

# Platform detection — same logic as Rack's arch.mk
MACHINE := $(shell $(CC) -dumpmachine)
ifneq (,$(findstring -darwin,$(MACHINE)))
# macOS: Cocoa + projectM v3 via Homebrew
CXXFLAGS += -I$(PROJECTM_PREFIX)/include
LDFLAGS  += -L$(PROJECTM_PREFIX)/lib -lprojectM -framework OpenGL -framework Cocoa
SOURCES  += src/PDWindow.mm
else ifneq (,$(findstring -linux,$(MACHINE)))
# Linux: GLFW offscreen + projectM v3 via apt (libprojectm-dev)
CXXFLAGS += $(shell pkg-config --cflags libprojectM 2>/dev/null || echo "-I/usr/include/libprojectM")
LDFLAGS  += $(shell pkg-config --libs   libprojectM 2>/dev/null || echo "-lprojectM") -lGL
SOURCES  += src/PDWindow_glfw.cpp
else
# Windows (MSYS2 UCRT64): GLFW offscreen + projectM v3
CXXFLAGS += -I/ucrt64/include/libprojectM
LDFLAGS  += -lprojectM -lopengl32
SOURCES  += src/PDWindow_glfw.cpp
endif

include $(RACK_DIR)/plugin.mk
