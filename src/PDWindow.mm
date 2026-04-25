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
		if (!wantOpen) return;

		NSScreen* screen = [NSScreen mainScreen];
		NSRect frame = [screen frame];

		NSWindow* w = [[NSWindow alloc]
			initWithContentRect:frame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];

		[w setLevel:NSFloatingWindowLevel];
		[w setIgnoresMouseEvents:YES];
		[w setOpaque:NO];
		[w setBackgroundColor:[NSColor clearColor]];
		[w setCollectionBehavior:
			NSWindowCollectionBehaviorCanJoinAllSpaces |
			NSWindowCollectionBehaviorStationary];

		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24,
			NSOpenGLPFAAlphaSize,  8,
			NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* v = [[PDGLView alloc] initWithFrame:frame pixelFormat:fmt];
		[v setWantsBestResolutionOpenGLSurface:NO];
		[w setContentView:v];
		[v prepareOpenGL];
		[w orderFront:nil];

		// Cache VCV Rack's window frame immediately
		NSWindow* rack = [NSApp mainWindow];
		if (rack) {
			NSRect f = rack.frame;
			rackX = f.origin.x;
			rackY = f.origin.y;
			rackW = f.size.width;
			rackH = f.size.height;
		}

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
	if (!running) return;

	PDGLView* v = (__bridge PDGLView*)view;
	NSOpenGLContext* ctx = [v openGLContext];
	[ctx makeCurrentContext];

	NSRect frame = [v frame];
	int screenW = (int)frame.size.width;
	int screenH = (int)frame.size.height;

	projectM::Settings s;
	s.windowWidth  = screenW;
	s.windowHeight = screenH;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);

	int frameCount = 0;
	std::atomic<float>* arx = &rackX, *ary = &rackY, *arw = &rackW, *arh = &rackH;

	while (running) {
		// Refresh rack window frame once per second from main thread
		if (++frameCount % 60 == 0) {
			dispatch_async(dispatch_get_main_queue(), ^{
				NSWindow* rack = [NSApp mainWindow];
				if (rack) {
					NSRect f = rack.frame;
					*arx = f.origin.x;
					*ary = f.origin.y;
					*arw = f.size.width;
					*arh = f.size.height;
				}
			});
		}

		if (requestNext.exchange(false)) pm->selectNext(true);
		if (requestPrev.exchange(false)) pm->selectPrevious(true);

		pm->renderFrame();

		// Punch a transparent hole where VCV Rack's window sits
		float rx = rackX, ry = rackY, rw = rackW, rh = rackH;
		if (rw > 0 && rh > 0) {
			glEnable(GL_SCISSOR_TEST);
			glScissor((int)rx, (int)ry, (int)rw, (int)rh);
			glClearColor(0.f, 0.f, 0.f, 0.f);
			glClear(GL_COLOR_BUFFER_BIT);
			glDisable(GL_SCISSOR_TEST);
		}

		[ctx flushBuffer];
	}

	delete pm;
}
