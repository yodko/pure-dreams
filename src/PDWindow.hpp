#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

struct PDWindow {
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};
	std::thread renderThread;
	void* win  = nullptr;
	void* view = nullptr;

	std::mutex readyMutex;
	std::condition_variable readyCv;
	bool viewReady = false;

	std::mutex pixelMutex;
	std::vector<uint8_t> pixels;
	int pixelW = 1920;
	int pixelH = 1080;
	bool pixelsDirty = false;


	void open();
	void close();
	void loop();
};
