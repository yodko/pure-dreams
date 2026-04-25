RACK_DIR ?= ../Rack

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# libprojectM
CXXFLAGS += -I$(RACK_DIR)/dep/include
LDFLAGS += -lprojectM-4

include $(RACK_DIR)/plugin.mk
