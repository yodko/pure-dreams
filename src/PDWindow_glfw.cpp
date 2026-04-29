// PDWindow_glfw.cpp — unified projectM v4 implementation for macOS, Linux, Windows
//
// Replaces both PDWindow.mm (Cocoa) and the old stub.
// Uses a hidden GLFW window for the offscreen OpenGL context — the same
// GLFW that VCV Rack already bundles. No Cocoa, no dispatch_async, no
// platform-specific threading tricks: GLFW handles all of that.
//
// Thread model:
//   open()  — called on the UI thread (PureDreamsWidget constructor)
//             creates the hidden GLFW window, then spawns the render thread
//   loop()  — render thread: makes the offscreen context current, runs
//             projectM, reads pixels back, stores them for draw()
//   close() — called on the UI thread (~PureDreamsWidget): signals the
//             render thread to stop, joins it, destroys the GLFW window

#include "PDWindow.hpp"
#include <projectM-4/projectM.h>
#include <GLFW/glfw3.h>

#include <dirent.h>
#include <algorithm>

// Preset directory is set via PDWindow::presetDir before open() is called.
// PureDreamsWidget sets it to asset::plugin(pluginInstance, "res/presets")
// so bundled presets are used on all platforms with no user setup.

// Scan a directory for .milk preset files and return their full paths sorted.
static std::vector<std::string> scan_presets(const char* dir) {
    std::vector<std::string> paths;
    DIR* d = opendir(dir);
    if (!d) return paths;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.substr(n.size() - 5) == ".milk")
            paths.push_back(std::string(dir) + "/" + n);
    }
    closedir(d);
    std::sort(paths.begin(), paths.end());
    return paths;
}

// ── addSample ────────────────────────────────────────────────────────────────

void PDWindow::addSample(float L, float R) {
    if (closing) return;
    std::lock_guard<std::mutex> lock(pcmMutex);
    if (pcmPos < PCM_SIZE * 2) {
        pcmBuf[pcmPos++] = L;
        pcmBuf[pcmPos++] = R;
        if (pcmPos >= PCM_SIZE * 2) {
            pcmReady = true;
            pcmPos   = 0;
        }
    }
}

// ── open ─────────────────────────────────────────────────────────────────────

void PDWindow::open() {
    pixels.resize(pixelW * pixelH * 4, 0);
    for (int i = 0; i < pixelW * pixelH * 4; i += 4) {
        pixels[i+0] = 10; pixels[i+1] = 10; pixels[i+2] = 14; pixels[i+3] = 255;
    }
    pixelsDirty = true;

    // Create a hidden GLFW window — this is our offscreen GL context.
    // open() is called on the UI/main thread, which is the only thread
    // allowed to call glfwCreateWindow.
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* offscreen = glfwCreateWindow(pixelW, pixelH, "projectM", nullptr, nullptr);
    glfwDefaultWindowHints();   // restore Rack's defaults

    win = (void*)offscreen;

    {
        std::lock_guard<std::mutex> lock(readyMutex);
        viewReady = true;
    }
    readyCv.notify_one();
    running      = true;
    renderThread = std::thread([this]() { loop(); });
}

// ── close ────────────────────────────────────────────────────────────────────

void PDWindow::close() {
    closing  = true;
    wantOpen = false;
    running  = false;
    {
        std::lock_guard<std::mutex> lock(readyMutex);
        viewReady = true;
    }
    readyCv.notify_all();
    if (renderThread.joinable()) renderThread.join();

    // Must be called from the main thread — satisfied because close() is
    // called from ~PureDreamsWidget() which runs on the UI thread.
    if (win) {
        glfwDestroyWindow((GLFWwindow*)win);
        win = nullptr;
    }
}

// ── loop (render thread) ─────────────────────────────────────────────────────

void PDWindow::loop() {
    {
        std::unique_lock<std::mutex> lock(readyMutex);
        readyCv.wait(lock, [this] { return viewReady; });
    }
    if (!running) return;

    GLFWwindow* offscreen = (GLFWwindow*)win;
    glfwMakeContextCurrent(offscreen);

    // Create projectM instance. The GL context must be current before this
    // call so projectM can resolve OpenGL function pointers internally.
    projectm_handle pm = projectm_create();
    if (!pm) {
        running = false;
        return;
    }

    projectm_set_window_size(pm, (size_t)pixelW, (size_t)pixelH);
    projectm_set_mesh_size(pm, 32, 24);
    projectm_set_fps(pm, 60);

    // Disable every auto-switch path projectM v4 has.
    // set_preset_locked(true) alone should be enough per the docs ("disables
    // both hard and soft cuts"), but in practice a preset still flips after
    // ~2s on Windows — likely a hard-cut beat-detect path that the lock
    // doesn't gate in this build. So we belt-and-suspenders it.
    projectm_set_preset_duration(pm, 86400.0);
    projectm_set_soft_cut_duration(pm, 0.0);
    projectm_set_hard_cut_enabled(pm, false);
    projectm_set_hard_cut_duration(pm, 86400.0);
    projectm_set_preset_locked(pm, true);

    // Scan preset directory and load the first preset.
    // projectm_load_preset_file() resets the "locked" flag, so we re-lock
    // after every load to keep auto-switching disabled — without this,
    // projectM's soft-cut timer fires after a couple of seconds and silently
    // switches preset, regardless of the duration we set above.
    std::vector<std::string> presetPaths = scan_presets(presetDir.c_str());
    int presetIdx = 0;
    if (!presetPaths.empty()) {
        auto it = std::find_if(presetPaths.begin(), presetPaths.end(),
            [](const std::string& p) { return p.find(DEFAULT_PRESET_HINT) != std::string::npos; });
        if (it != presetPaths.end()) presetIdx = (int)std::distance(presetPaths.begin(), it);
        projectm_load_preset_file(pm, presetPaths[presetIdx].c_str(), false);
        projectm_set_preset_locked(pm, true);
        std::lock_guard<std::mutex> nl(nameMutex);
        currentPresetName  = presetPaths[presetIdx];
        currentPresetIndex = presetIdx;
    }

    while (running) {

        // Next / prev / direct index requests
        if (requestNext.exchange(false) && !presetPaths.empty()) {
            presetIdx = (presetIdx + 1) % (int)presetPaths.size();
            projectm_load_preset_file(pm, presetPaths[presetIdx].c_str(), true);
            projectm_set_preset_locked(pm, true);
            currentPresetIndex = presetIdx;
            std::lock_guard<std::mutex> nl(nameMutex);
            currentPresetName = presetPaths[presetIdx];
        }
        if (requestPrev.exchange(false) && !presetPaths.empty()) {
            presetIdx = (presetIdx - 1 + (int)presetPaths.size()) % (int)presetPaths.size();
            projectm_load_preset_file(pm, presetPaths[presetIdx].c_str(), true);
            projectm_set_preset_locked(pm, true);
            currentPresetIndex = presetIdx;
            std::lock_guard<std::mutex> nl(nameMutex);
            currentPresetName = presetPaths[presetIdx];
        }
        int req = requestPreset.exchange(-1);
        if (req >= 0 && req < (int)presetPaths.size()) {
            presetIdx = req;
            projectm_load_preset_file(pm, presetPaths[presetIdx].c_str(), true);
            projectm_set_preset_locked(pm, true);
            currentPresetIndex = presetIdx;
            std::lock_guard<std::mutex> nl(nameMutex);
            currentPresetName = presetPaths[presetIdx];
        }

        // Feed audio
        {
            std::lock_guard<std::mutex> lock(pcmMutex);
            if (pcmReady) {
                projectm_pcm_add_float(pm, pcmBuf, (unsigned int)PCM_SIZE,
                                       PROJECTM_STEREO);
                pcmReady = false;
            }
        }

        // Render
        projectm_opengl_render_frame(pm);

        // Read pixels back for NanoVG upload
        {
            std::lock_guard<std::mutex> lock(pixelMutex);
            glReadPixels(0, 0, pixelW, pixelH, GL_RGBA, GL_UNSIGNED_BYTE,
                         pixels.data());
            pixelsDirty = true;
        }

        glfwSwapBuffers(offscreen);
    }

    // v4 destroy is safe — no destructor crash
    projectm_destroy(pm);
    glfwMakeContextCurrent(nullptr);
}
