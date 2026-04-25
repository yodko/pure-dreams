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

void PDWindow::open() {
	dispatch_async(dispatch_get_main_queue(), ^{
		// If close() was called before this block ran, bail out
		if (!wantOpen) return;

		NSScreen* screen = [NSScreen mainScreen];
		NSRect frame = [screen frame];

		NSWindow* w = [[NSWindow alloc]
			initWithContentRect:frame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];

		[w setLevel:NSNormalWindowLevel];
		[w setIgnoresMouseEvents:YES];
		[w setCollectionBehavior:
			NSWindowCollectionBehaviorCanJoinAllSpaces |
			NSWindowCollectionBehaviorStationary];

		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* v = [[PDGLView alloc] initWithFrame:frame pixelFormat:fmt];
		[w setContentView:v];
		[v prepareOpenGL];
		[w orderFront:nil];

		// Bring VCV Rack back to front so it sits above our window
		NSWindow* rack = [NSApp mainWindow];
		if (rack) [rack makeKeyAndOrderFront:nil];

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

	// Unblock loop() if it's waiting for viewReady
	{
		std::lock_guard<std::mutex> lock(readyMutex);
		viewReady = true;
	}
	readyCv.notify_all();

	if (renderThread.joinable()) renderThread.join();

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

	if (!running) return;  // closed before view was ready

	PDGLView* v = (__bridge PDGLView*)view;
	NSOpenGLContext* ctx = [v openGLContext];
	[ctx makeCurrentContext];

	NSRect frame = [v frame];
	projectM::Settings s;
	s.windowWidth  = (int)frame.size.width;
	s.windowHeight = (int)frame.size.height;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);

	while (running) {
		if (requestNext.exchange(false)) pm->selectNext(true);
		if (requestPrev.exchange(false)) pm->selectPrevious(true);
		pm->renderFrame();
		[ctx flushBuffer];
	}

	delete pm;
}
