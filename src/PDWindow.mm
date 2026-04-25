#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#include <libprojectM/projectM.hpp>
#include "PDWindow.hpp"

@interface PDGLView : NSOpenGLView
@end
@implementation PDGLView
- (BOOL)acceptsFirstResponder { return YES; }
@end

void PDWindow::open() {
	dispatch_async(dispatch_get_main_queue(), ^{
		NSRect frame = NSMakeRect(100, 100, 1280, 720);
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
		[w makeKeyAndOrderFront:nil];

		win  = (__bridge void*)w;
		view = (__bridge void*)v;

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
