#include "PDWindow.hpp"

// No-op implementation for non-macOS platforms.
// The background visualiser requires projectM and NSOpenGLView (macOS only).
// On Linux and Windows the module loads and the panel is functional;
// the rack background simply remains dark.

void PDWindow::addSample(float, float) {}

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);
	for (int i = 0; i < pixelW * pixelH * 4; i += 4) {
		pixels[i+0] = 10; pixels[i+1] = 10; pixels[i+2] = 14; pixels[i+3] = 255;
	}
	pixelsDirty = true;
}

void PDWindow::close() {
	running = false;
}

void PDWindow::loop() {}
