#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGL/gl.h>
#include <libprojectM/projectM.hpp>
#include "PDWindow.hpp"
#include <mutex>
#include <condition_variable>
#include <algorithm>

@interface PDGLView : NSOpenGLView
@end
@implementation PDGLView
- (BOOL)acceptsFirstResponder { return NO; }
@end

// CADisplayLink-driven view — pulls from pixel buffer on main thread
@interface PDDisplayView : NSView {
	CADisplayLink* _link;
}
@property (assign) PDWindow* pdWindow;
- (void)startLink;
- (void)stopLink;
@end

@implementation PDDisplayView
- (instancetype)initWithFrame:(NSRect)frame pdWindow:(PDWindow*)win {
	self = [super initWithFrame:frame];
	_pdWindow = win;
	self.wantsLayer = YES;
	self.layer.opaque = NO;
	return self;
}
- (BOOL)isOpaque { return NO; }

- (void)startLink {
	_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
	[_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}
- (void)stopLink {
	[_link invalidate];
	_link = nil;
	_pdWindow = nullptr;
}
- (void)tick:(CADisplayLink*)sender {
	PDWindow* pw = _pdWindow;
	if (!pw) return;
	std::lock_guard<std::mutex> lock(pw->pixelMutex);
	if (!pw->pixelsDirty || pw->pixels.empty()) return;
	pw->pixelsDirty = false;

	int w = pw->pixelW, h = pw->pixelH;
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(
		pw->pixels.data(), w, h, 8, w*4, cs,
		kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast);
	CGColorSpaceRelease(cs);
	if (!ctx) return;
	CGImageRef img = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);

	CALayer* layer = self.layer;
	layer.contentsGravity = kCAGravityResize;
	layer.transform = CATransform3DMakeScale(1, -1, 1); // Y-flip
	layer.contents = (__bridge id)img;
	CGImageRelease(img);
}
@end

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);

	dispatch_async(dispatch_get_main_queue(), ^{
		if (!wantOpen) return;

		NSScreen* screen = [NSScreen mainScreen];
		NSRect screenFrame = [screen frame];

		// Offscreen render window
		NSRect tiny = NSMakeRect(-pixelW-10, -pixelH-10, pixelW, pixelH);
		NSWindow* rw = [[NSWindow alloc]
			initWithContentRect:tiny
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];
		NSOpenGLPixelFormatAttribute attrs[] = {
			NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
			NSOpenGLPFAColorSize, 24, NSOpenGLPFADepthSize, 16,
			NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
		};
		NSOpenGLPixelFormat* fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
		PDGLView* rv = [[PDGLView alloc] initWithFrame:tiny pixelFormat:fmt];
		[rw setContentView:rv];
		[rv prepareOpenGL];
		[rw orderFront:nil];
		renderWin  = (__bridge void*)rw;
		renderView = (__bridge void*)rv;

		// Display window (floating, transparent, mouse pass-through)
		NSWindow* dw = [[NSWindow alloc]
			initWithContentRect:screenFrame
			styleMask:NSWindowStyleMaskBorderless
			backing:NSBackingStoreBuffered defer:NO];
		[dw setLevel:NSFloatingWindowLevel];
		[dw setOpaque:NO];
		[dw setIgnoresMouseEvents:YES];
		[dw setBackgroundColor:[NSColor clearColor]];
		[dw setCollectionBehavior:
			NSWindowCollectionBehaviorCanJoinAllSpaces |
			NSWindowCollectionBehaviorStationary];
		PDDisplayView* dv = [[PDDisplayView alloc]
			initWithFrame:screenFrame pdWindow:this];
		[dw setContentView:dv];
		[dw orderFront:nil];
		[dv startLink];
		displayWin  = (__bridge void*)dw;
		displayView = (__bridge void*)dv;

		// Cache rack frame
		NSWindow* rack = [NSApp mainWindow];
		if (rack) {
			NSRect f = rack.frame;
			rackX = f.origin.x; rackY = f.origin.y;
			rackW = f.size.width; rackH = f.size.height;
		}

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

	// Stop display link on main thread (we ARE on main thread from destructor)
	if (displayView) {
		[(__bridge PDDisplayView*)displayView stopLink];
	}

	if (renderThread.joinable()) renderThread.join();

	void* rw = renderWin, *dw = displayWin;
	renderWin = renderView = displayWin = displayView = nullptr;
	if (rw || dw) {
		dispatch_async(dispatch_get_main_queue(), ^{
			if (rw) [(__bridge NSWindow*)rw close];
			if (dw) [(__bridge NSWindow*)dw close];
		});
	}
}

void PDWindow::loop() {
	{
		std::unique_lock<std::mutex> lock(readyMutex);
		readyCv.wait(lock, [this]{ return viewReady; });
	}
	if (!running) return;

	PDGLView* rv = (__bridge PDGLView*)renderView;
	NSOpenGLContext* ctx = [rv openGLContext];
	[ctx makeCurrentContext];

	projectM::Settings s;
	s.windowWidth  = pixelW;
	s.windowHeight = pixelH;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);

	int frameCount = 0;
	std::atomic<float>* arx = &rackX, *ary = &rackY, *arw = &rackW, *arh = &rackH;

	while (running) {
		// Refresh rack frame once per second from main thread
		if (++frameCount % 60 == 0) {
			dispatch_async(dispatch_get_main_queue(), ^{
				NSWindow* rack = [NSApp mainWindow];
				if (rack) {
					NSRect f = rack.frame;
					*arx = f.origin.x; *ary = f.origin.y;
					*arw = f.size.width; *arh = f.size.height;
				}
			});
		}

		if (requestNext.exchange(false)) pm->selectNext(true);
		if (requestPrev.exchange(false)) pm->selectPrevious(true);

		pm->renderFrame();

		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

			// Zero (transparent) the VCV Rack window area
			int x0 = std::max(0, (int)rackX.load());
			int y0 = std::max(0, (int)rackY.load());
			int x1 = std::min(pixelW, x0 + (int)rackW.load());
			int y1 = std::min(pixelH, y0 + (int)rackH.load());
			for (int y = y0; y < y1; y++) {
				memset(pixels.data() + y * pixelW * 4 + x0 * 4, 0, (x1 - x0) * 4);
			}
			pixelsDirty = true;
		}

		[ctx flushBuffer];
	}

	delete pm;
}
