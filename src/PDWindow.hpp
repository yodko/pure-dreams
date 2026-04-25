#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

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

	void open();
	void close();
	void loop();
};
