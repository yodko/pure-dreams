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

		// keep window hidden — we only need the GL context
		[w setAlphaValue:0.0];
		[w orderFront:nil];

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

	// Create FBO so projectM renders into a texture we can read
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &fboTex);
	glBindTexture(GL_TEXTURE_2D, fboTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pixelW, pixelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	projectM::Settings s;
	s.windowWidth  = pixelW;
	s.windowHeight = pixelH;
	s.fps = 60; s.meshX = 32; s.meshY = 24;
	projectM* pm = new projectM(s);

	while (running) {
		if (requestNext.exchange(false)) pm->selectNext(true);
		if (requestPrev.exchange(false)) pm->selectPrevious(true);

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		pm->renderFrame();

		{
			std::lock_guard<std::mutex> lock(pixelMutex);
			glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			pixelsDirty = true;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		[ctx flushBuffer];
	}

	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &fboTex);
	delete pm;
}
