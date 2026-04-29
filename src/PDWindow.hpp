#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <cstdint>

struct PDWindow {
	std::atomic<bool> wantOpen{true};
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
	std::atomic<int>  requestPreset{-1};
	std::atomic<bool> closing{false};

	std::mutex nameMutex;
	std::string currentPresetName;
	std::atomic<int> currentPresetIndex{0};

	// Set before calling open() — path to directory containing .milk files
	std::string presetDir;

	// PCM audio feed (stereo interleaved, written by audio thread)
	static const int PCM_SIZE = 512;
	std::mutex pcmMutex;
	float pcmBuf[PCM_SIZE * 2] = {};
	int   pcmPos = 0;
	bool  pcmReady = false;

	void addSample(float L, float R);

	void open();
	void close();
	void loop();
};
