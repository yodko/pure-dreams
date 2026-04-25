RACK_DIR ?= ../Rack

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp
SOURCES += src/PDWindow.mm

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

PROJECTM_PREFIX ?= /opt/homebrew/Cellar/projectm/3.1.12
CXXFLAGS += -I$(PROJECTM_PREFIX)/include
LDFLAGS += -L$(PROJECTM_PREFIX)/lib -lprojectM -framework OpenGL -framework Cocoa

include $(RACK_DIR)/plugin.mk
