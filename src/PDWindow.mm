#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGL/gl.h>
#include <libprojectM/projectM.hpp>
#include "PDWindow.hpp"
#include <mutex>
#include <condition_variable>

// ── Offscreen GL view ────────────────────────────────────────────────────────

@interface PDGLView : NSOpenGLView
@end
@implementation PDGLView
- (BOOL)acceptsFirstResponder { return NO; }
@end

// ── Display view (CALayer backed, supports proper transparency) ──────────────

@interface PDDisplayView : NSView
@property (assign) PDWindow* pdWindow;
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
- (void)updateFromPixels {
	PDWindow* pw = _pdWindow;
	if (!pw) return;
	std::lock_guard<std::mutex> lock(pw->pixelMutex);
	if (pw->pixels.empty() || !pw->pixelsDirty) return;
	pw->pixelsDirty = false;

	int w = pw->pixelW, h = pw->pixelH;
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	// Use premultiplied alpha so transparent pixels composite correctly
	CGContextRef ctx = CGBitmapContextCreate(
		pw->pixels.data(), w, h, 8, w * 4, cs,
		kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast);
	CGColorSpaceRelease(cs);
	if (!ctx) return;

	CGImageRef img = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);

	// Flip Y (GL has origin at bottom-left, Core Graphics at top-left)
	CALayer* layer = self.layer;
	layer.contentsGravity = kCAGravityResizeAspectFill;
	layer.contents = (__bridge id)img;
	layer.contentsRect = CGRectMake(0, 0, 1, 1);
	// Apply Y-flip transform on the layer
	layer.transform = CATransform3DMakeScale(1, -1, 1);
	CGImageRelease(img);
}
@end

// ── PDWindow ─────────────────────────────────────────────────────────────────

void PDWindow::open() {
	pixels.resize(pixelW * pixelH * 4, 0);

	dispatch_async(dispatch_get_main_queue(), ^{
		if (!wantOpen) return;

		NSScreen* screen = [NSScreen mainScreen];
		NSRect screenFrame = [screen frame];

		// --- Offscreen render window (tiny, off-screen) ---
		NSRect tiny = NSMakeRect(-pixelW - 10, -pixelH - 10, pixelW, pixelH);
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

		// --- Display window (fullscreen, transparent, NSFloatingWindowLevel) ---
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
	if (renderThread.joinable()) renderThread.join();

	void* rw = renderWin,  *dw = displayWin;
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
		// Refresh rack frame once per second
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

		// Read pixels and zero out the VCV Rack area
		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

			// Zero (transparent) the rack window area in the pixel buffer
			float rx = rackX, ry = rackY, rw2 = rackW, rh2 = rackH;
			// Convert screen coords (bottom-left) to pixel buffer coords
			// pixelH = screenH logically, so y maps directly
			int x0 = (int)rx, y0 = (int)ry;
			int x1 = x0 + (int)rw2, y1 = y0 + (int)rh2;
			x0 = std::max(0, x0); y0 = std::max(0, y0);
			x1 = std::min(pixelW, x1); y1 = std::min(pixelH, y1);
			for (int y = y0; y < y1; y++) {
				int row = y * pixelW * 4;
				for (int x = x0; x < x1; x++) {
					pixels[row + x*4 + 0] = 0;
					pixels[row + x*4 + 1] = 0;
					pixels[row + x*4 + 2] = 0;
					pixels[row + x*4 + 3] = 0;
				}
			}
			pixelsDirty = true;
		}

		[ctx flushBuffer];

		// Update display on main thread
		void* dv = displayView;
		if (dv) {
			dispatch_async(dispatch_get_main_queue(), ^{
				[(__bridge PDDisplayView*)dv updateFromPixels];
			});
		}
	}

	delete pm;
}
