#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <libprojectM/projectM.hpp>
#include "PDWindow.hpp"
#include <mutex>
#include <condition_variable>

@interface PDGLView : NSOpenGLView
@end
@implementation PDGLView
- (BOOL)acceptsFirstResponder { return NO; }
@end

void PDWindow::addSample(float L, float R) {
	std::lock_guard<std::mutex> lock(pcmMutex);
	if (pcmPos < PCM_SIZE * 2) {
		pcmBuf[pcmPos++] = L;
		pcmBuf[pcmPos++] = R;
		if (pcmPos >= PCM_SIZE * 2) {
			pcmReady = true;
			pcmPos = 0;
		}
	}
}

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);
	// Pre-fill with dark colour so something shows before projectM starts
	for (int i = 0; i < pixelW * pixelH * 4; i += 4) {
		pixels[i+0] = 10; pixels[i+1] = 10; pixels[i+2] = 14; pixels[i+3] = 255;
	}
	pixelsDirty = true;

	dispatch_async(dispatch_get_main_queue(), ^{
		if (!wantOpen) return;

		// Offscreen render window — completely hidden from Exposé/Mission Control
		NSRect frame = NSMakeRect(0, 0, pixelW, pixelH);
		NSWindow* w = [[NSWindow alloc]
			initWithContentRect:frame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];
		// Hide from window management entirely
		[w setCollectionBehavior:
			NSWindowCollectionBehaviorTransient |
			NSWindowCollectionBehaviorIgnoresCycle |
			NSWindowCollectionBehaviorStationary];
		[w setExcludedFromWindowsMenu:YES];

		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* v = [[PDGLView alloc] initWithFrame:frame pixelFormat:fmt];
		[w setContentView:v];
		[v prepareOpenGL];
		[w orderFront:nil];   // show briefly to initialise GL
		[w orderOut:nil];     // then hide completely — GL context still active

		win  = (__bridge void*)w;
		view = (__bridge void*)v;

		{
			std::lock_guard<std::mutex> lock(readyMutex);
			viewReady = true;
		}
		readyCv.notify_one();
		running = true;
		renderThread = std::thread([this]() { loop(); });
	});
}

void PDWindow::close() {
	wantOpen = false;
	running  = false;
	{
		std::lock_guard<std::mutex> lock(readyMutex);
		viewReady = true;
	}
	readyCv.notify_all();
	if (renderThread.joinable()) renderThread.join();

	void* w = win;
	win = view = nullptr;
	if (w) {
		dispatch_async(dispatch_get_main_queue(), ^{
			[(__bridge NSWindow*)w close];
		});
	}
}

void PDWindow::loop() {
	{
		std::unique_lock<std::mutex> lock(readyMutex);
		readyCv.wait(lock, [this]{ return viewReady; });
	}
	if (!running) return;

	PDGLView* v = (__bridge PDGLView*)view;
	NSOpenGLContext* ctx = [v openGLContext];
	[ctx makeCurrentContext];

	projectM::Settings s;
	s.windowWidth  = pixelW;
	s.windowHeight = pixelH;
	// Use subdirectory with only .milk presets — avoids .dylib plugin crashes
	s.presetURL      = "/opt/homebrew/Cellar/projectm/3.1.12/share/projectM/presets/presets_bltc201";
	s.shuffleEnabled = false;
	s.presetDuration = 86400;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);
	pm->setPresetLock(true); // stay on chosen preset — unlock only for next/prev

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

		// Feed audio to projectM
		{
			std::lock_guard<std::mutex> lock(pcmMutex);
			if (pcmReady) {
				pm->pcm()->addPCMfloat_2ch(pcmBuf, PCM_SIZE);
				pcmReady = false;
			}
		}

		pm->renderFrame();

		// Update current preset name + index
		unsigned int idx = 0;
		if (pm->selectedPresetIndex(idx)) {
			currentPresetIndex = (int)idx;
			std::string name = pm->getPresetName(idx);
			std::lock_guard<std::mutex> nl(nameMutex);
			currentPresetName = name;
		}

		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			pixelsDirty = true;
		}

		[ctx flushBuffer];
	}

	// Finish all pending GL operations.
	// We intentionally don't call delete pm here — projectM v3's destructor
	// has a vtable crash on macOS ARM64. The GL context closes with the window
	// and the OS reclaims all memory on exit.
	glFinish();
	pm = nullptr;
}
