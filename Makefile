RACK_DIR ?= ../Rack
include $(RACK_DIR)/arch.mk

SOURCES += src/plugin.cpp
SOURCES += src/PureDreams.cpp
SOURCES += src/PDWindow_glfw.cpp   # unified GLFW + projectM v4, all platforms

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# ── projectM v4 static library (built from source via `make dep`) ────────────

projectm_lib := dep/projectm/build/lib/libprojectM-4.a

FLAGS   += -I./dep/projectm/build/include -DprojectM_api_EXPORTS
OBJECTS += $(projectm_lib)
DEPS    += $(projectm_lib)

ifdef ARCH_WIN
	FLAGS   += -D_USE_MATH_DEFINES
	LDFLAGS += -lopengl32
else ifdef ARCH_MAC
	LDFLAGS += -framework OpenGL
else
	LDFLAGS += -lGL
endif

$(projectm_lib):
	cp -r src/dep/projectm dep/
	mkdir -p dep/projectm/build
ifdef ARCH_WIN
	cp src/dep/FileScanner.cpp dep/projectm/src/libprojectM/Renderer/ 2>/dev/null || true
	cd dep/projectm/build && cmake \
		-DINCLUDE_DIR=$(RACK_DIR)/dep/include \
		-DCMAKE_INSTALL_LIBDIR=lib \
		-DBUILD_SHARED_LIBS=OFF \
		-D_FILE_OFFSET_BITS=64 \
		-DCMAKE_CXX_STANDARD_LIBRARIES=-lpsapi \
		-DENABLE_OPENMP=OFF \
		-DENABLE_THREADING=OFF \
		-DENABLE_PLAYLIST=OFF \
		-DCMAKE_INSTALL_PREFIX=. \
		..
	cp $(RACK_DIR)/libRack.dll.a dep/ 2>/dev/null || true
else
	cd dep/projectm/build && cmake \
		-DINCLUDE_DIR=$(RACK_DIR)/dep/include \
		-DBUILD_SHARED_LIBS=OFF \
		-DENABLE_SDL_UI=OFF \
		-DENABLE_OPENMP=OFF \
		-DCMAKE_BUILD_TYPE=Release \
		-DENABLE_THREADING=OFF \
		-DENABLE_PLAYLIST=OFF \
		-DCMAKE_INSTALL_PREFIX=. \
		..
endif
	cd dep/projectm/build && cmake --build . -- -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	cd dep/projectm/build && cmake --build . --target install

include $(RACK_DIR)/plugin.mk
