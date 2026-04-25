#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

struct PDWindow {
	std::atomic<bool> wantOpen{true};
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};
	std::thread renderThread;

	// Offscreen GL render window
	void* renderWin  = nullptr;
	void* renderView = nullptr;

	// Display window (CALayer-backed, transparent)
	void* displayWin  = nullptr;
	void* displayView = nullptr;

	std::mutex readyMutex;
	std::condition_variable readyCv;
	bool viewReady = false;

	// Pixel buffer shared between render thread and display
	std::mutex pixelMutex;
	std::vector<uint8_t> pixels;
	int pixelW = 1920;
	int pixelH = 1080;
	bool pixelsDirty = false;

	// VCV Rack window frame (screen coords, bottom-left origin)
	std::atomic<float> rackX{0}, rackY{0}, rackW{0}, rackH{0};

	void open();
	void close();
	void loop();
};
