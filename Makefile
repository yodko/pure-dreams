RACK_DIR ?= ../Rack

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

PROJECTM_PREFIX ?= /opt/homebrew/Cellar/projectm/3.1.12

# Platform detection — same logic as Rack's arch.mk
MACHINE := $(shell $(CC) -dumpmachine)
ifneq (,$(findstring -darwin,$(MACHINE)))
CXXFLAGS += -I$(PROJECTM_PREFIX)/include
LDFLAGS  += -L$(PROJECTM_PREFIX)/lib -lprojectM -framework OpenGL -framework Cocoa
SOURCES  += src/PDWindow.mm
else
SOURCES  += src/PDWindow_stub.cpp
endif

include $(RACK_DIR)/plugin.mk
