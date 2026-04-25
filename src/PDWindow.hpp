#pragma once
#include <atomic>
#include <thread>

struct PDWindow {
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};
	std::thread renderThread;
	void* win  = nullptr;
	void* view = nullptr;

	void open();
	void close();
	void loop();
};
