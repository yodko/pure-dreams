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
- (BOOL)acceptsFirstResponder { return YES; }
@end

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);

	dispatch_async(dispatch_get_main_queue(), ^{
		NSRect frame = NSMakeRect(0, 0, pixelW, pixelH);
		NSWindow* w = [[NSWindow alloc]
			initWithContentRect:frame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];

		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* v = [[PDGLView alloc] initWithFrame:frame pixelFormat:fmt];
		[w setContentView:v];
		[v prepareOpenGL];

		// Move off-screen so macOS allocates a real render surface
		[w setFrameOrigin:NSMakePoint(-pixelW - 10, -pixelH - 10)];
		[w makeKeyAndOrderFront:nil];

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
	running = false;
	if (renderThread.joinable()) renderThread.join();

	// Capture raw pointer by value before dispatch — PDWindow may be
	// deleted before the block executes, so we must not touch members.
	void* w = win;
	win  = nullptr;
	view = nullptr;

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

	PDGLView* v = (__bridge PDGLView*)view;
	NSOpenGLContext* ctx = [v openGLContext];
	[ctx makeCurrentContext];

	projectM::Settings s;
	s.windowWidth  = pixelW;
	s.windowHeight = pixelH;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);

	while (running) {
		if (requestNext.exchange(false)) pm->selectNext(true);
		if (requestPrev.exchange(false)) pm->selectPrevious(true);

		pm->renderFrame();

		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			pixelsDirty = true;
		}

		[ctx flushBuffer];
	}

	delete pm;
}
