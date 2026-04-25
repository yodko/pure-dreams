#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
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
	dispatch_async(dispatch_get_main_queue(), ^{
		NSRect frame = [[NSScreen mainScreen] frame];
		NSWindow* w = [[NSWindow alloc]
			initWithContentRect:frame
			styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskResizable|NSWindowStyleMaskClosable
			backing:NSBackingStoreBuffered defer:NO];
		[w setTitle:@"Pure Dreams"];

		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* v = [[PDGLView alloc] initWithFrame:frame pixelFormat:fmt];
		[w setContentView:v];
		[w setLevel:NSNormalWindowLevel - 1];
		[w makeKeyAndOrderFront:nil];
		NSWindow* main = [NSApp mainWindow];
		if (main) [w orderWindow:NSWindowBelow relativeTo:main.windowNumber];

		win  = (__bridge void*)w;
		view = (__bridge void*)v;

		[v prepareOpenGL];

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
	dispatch_async(dispatch_get_main_queue(), ^{
		if (win) {
			NSWindow* w = (__bridge NSWindow*)win;
			[w close];
			win = nullptr;
		}
		if (view) {
			(void)(__bridge PDGLView*)view;
			view = nullptr;
		}
	});
}

void PDWindow::loop() {
	{
		std::unique_lock<std::mutex> lock(readyMutex);
		readyCv.wait(lock, [this]{ return viewReady; });
	}

	PDGLView* v = (__bridge PDGLView*)view;
	NSOpenGLContext* ctx = [v openGLContext];
	[ctx makeCurrentContext];

	NSRect r = [v frame];
	projectM::Settings s;
	s.windowWidth  = (int)r.size.width;
	s.windowHeight = (int)r.size.height;
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
