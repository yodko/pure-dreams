RACK_DIR ?= ../Rack

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

PROJECTM_PREFIX ?= /opt/homebrew/Cellar/projectm/3.1.12
SDL2_PREFIX     ?= /opt/homebrew/Cellar/sdl2/2.32.10
CXXFLAGS += -I$(PROJECTM_PREFIX)/include -I$(SDL2_PREFIX)/include
LDFLAGS += -L$(PROJECTM_PREFIX)/lib -lprojectM -L$(SDL2_PREFIX)/lib -lSDL2 -framework OpenGL

include $(RACK_DIR)/plugin.mk
