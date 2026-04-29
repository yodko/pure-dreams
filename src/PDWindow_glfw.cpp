// PDWindow_glfw.cpp — Linux and Windows implementation
// Uses a hidden GLFW window as an offscreen OpenGL context.
// Mirrors the structure of PDWindow.mm exactly — same thread model,
// same glReadPixels pixel readback, same projectM v3 C++ API.

#define GL_SILENCE_DEPRECATION
#include "PDWindow.hpp"
#include <libprojectM/projectM.hpp>
#include <GLFW/glfw3.h>

// Default preset search paths per platform.
// User can override by symlinking or installing projectM to these locations.
#ifdef ARCH_WIN
static const char* DEFAULT_PRESET_DIR = "C:\\Program Files\\projectM\\presets";
#else
static const char* DEFAULT_PRESET_DIR = "/usr/share/projectM/presets";
#endif

void PDWindow::addSample(float L, float R) {
	if (closing) return;
	std::lock_guard<std::mutex> lock(pcmMutex);
	if (pcmPos < PCM_SIZE * 2) {
		pcmBuf[pcmPos++] = L;
		pcmBuf[pcmPos++] = R;
		if (pcmPos >= PCM_SIZE * 2) {
			pcmReady = true;
			pcmPos   = 0;
		}
	}
}

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);
	for (int i = 0; i < pixelW * pixelH * 4; i += 4) {
		pixels[i+0] = 10; pixels[i+1] = 10; pixels[i+2] = 14; pixels[i+3] = 255;
	}
	pixelsDirty = true;

	// Create a hidden window — this gives us an offscreen GL context without
	// any platform-specific window API.  open() is called on the UI thread
	// (from PureDreamsWidget constructor) so glfwCreateWindow is safe here.
	glfwWindowHint(GLFW_VISIBLE,                GLFW_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,  3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,  3);
	glfwWindowHint(GLFW_OPENGL_PROFILE,         GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* offscreen = glfwCreateWindow(pixelW, pixelH, "projectM", nullptr, nullptr);
	glfwDefaultWindowHints();   // restore defaults so Rack's own windows are unaffected

	win = (void*)offscreen;

	{
		std::lock_guard<std::mutex> lock(readyMutex);
		viewReady = true;
	}
	readyCv.notify_one();
	running = true;
	renderThread = std::thread([this]() { loop(); });
}

void PDWindow::close() {
	closing  = true;
	wantOpen = false;
	running  = false;
	{
		std::lock_guard<std::mutex> lock(readyMutex);
		viewReady = true;
	}
	readyCv.notify_all();
	if (renderThread.joinable()) renderThread.join();

	// glfwDestroyWindow must be called from the main thread.
	// close() is called from ~PureDreamsWidget() which runs on the UI thread.
	if (win) {
		glfwDestroyWindow((GLFWwindow*)win);
		win = nullptr;
	}
}

void PDWindow::loop() {
	{
		std::unique_lock<std::mutex> lock(readyMutex);
		readyCv.wait(lock, [this]{ return viewReady; });
	}
	if (!running) return;

	GLFWwindow* offscreen = (GLFWwindow*)win;
	glfwMakeContextCurrent(offscreen);

	projectM::Settings s;
	s.windowWidth    = pixelW;
	s.windowHeight   = pixelH;
	s.presetURL      = DEFAULT_PRESET_DIR;
	s.shuffleEnabled = false;
	s.presetDuration = 86400;
	s.fps            = 60;
	s.meshX          = 32;
	s.meshY          = 24;
	projectM* pm     = new projectM(s);
	pm->setPresetLock(true);

	while (running) {
		if (requestNext.exchange(false)) {
			pm->setPresetLock(false);
			pm->selectNext(true);
			pm->setPresetLock(true);
		}
		if (requestPrev.exchange(false)) {
			pm->setPresetLock(false);
			pm->selectPrevious(true);
			pm->setPresetLock(true);
		}
		int preset = requestPreset.exchange(-1);
		if (preset >= 0) {
			pm->setPresetLock(false);
			pm->selectPreset((unsigned)preset, true);
			pm->setPresetLock(true);
		}

		{
			std::lock_guard<std::mutex> lock(pcmMutex);
			if (pcmReady) {
				pm->pcm()->addPCMfloat_2ch(pcmBuf, PCM_SIZE);
				pcmReady = false;
			}
		}

		pm->renderFrame();

		unsigned int idx = 0;
		if (pm->selectedPresetIndex(idx)) {
			currentPresetIndex = (int)idx;
			std::string name   = pm->getPresetName(idx);
			std::lock_guard<std::mutex> nl(nameMutex);
			currentPresetName  = name;
		}

		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			pixelsDirty = true;
		}

		glfwSwapBuffers(offscreen);
	}

	// Match macOS: skip delete to avoid vtable crash in projectM v3 destructor.
	pm = nullptr;
	glfwMakeContextCurrent(nullptr);
}
