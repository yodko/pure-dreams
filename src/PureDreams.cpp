#include "plugin.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

// Forward declarations for libprojectM (linked at build time)
// We use the projectM 4.x C API
extern "C" {
#include <projectM-4/projectM.h>
}

static const int PANEL_HP = 128; // width in HP — covers a full standard rack row

struct PureDreams : Module {
	enum ParamId {
		BRIGHTNESS_PARAM,
		PREV_PARAM,
		NEXT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		LEFT_INPUT,
		RIGHT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 0.7f, "Brightness");
		configButton(PREV_PARAM, "Previous preset");
		configButton(NEXT_PARAM, "Next preset");
		configInput(LEFT_INPUT, "Left audio");
		configInput(RIGHT_INPUT, "Right audio");
	}

	void process(const ProcessArgs& args) override {
		// Handled in the widget render thread
	}
};

// ─── Renderer ────────────────────────────────────────────────────────────────
// Runs projectM in a background thread, blits to a shared pixel buffer.
// The widget reads the buffer each frame and uploads it as a NanoVG image.

struct ProjectMRenderer {
	std::thread renderThread;
	std::mutex bufferMutex;
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};
	std::atomic<float> brightness{0.7f};

	int width = 1280;
	int height = 720;
	std::vector<uint8_t> pixels; // RGBA, width*height*4

	projectm_handle pm = nullptr;

	void start() {
		pixels.resize(width * height * 4, 0);
		running = true;
		renderThread = std::thread([this]() { loop(); });
	}

	void stop() {
		running = false;
		if (renderThread.joinable())
			renderThread.join();
	}

	void loop() {
		// TODO: create an offscreen OpenGL context here (platform-specific)
		// For macOS: NSOpenGLContext or CGLContext
		// This is the main integration point — stubbed for now.

		projectm_settings* settings = projectm_alloc_settings();
		settings->window_width  = width;
		settings->window_height = height;
		settings->fps           = 60;
		settings->mesh_x        = 32;
		settings->mesh_y        = 24;
		pm = projectm_create_settings(settings, 0);
		projectm_free_settings(settings);

		if (!pm) return;

		while (running) {
			if (requestNext.exchange(false))
				projectm_select_next_preset(pm, true);
			if (requestPrev.exchange(false))
				projectm_select_previous_preset(pm, true);

			projectm_render_frame(pm);

			// Read pixels from the framebuffer
			{
				std::lock_guard<std::mutex> lock(bufferMutex);
				// glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
				// (Uncommented once GL context is wired up)
			}
		}

		projectm_destroy(pm);
		pm = nullptr;
	}

	void feedAudio(float left, float right, int count) {
		if (!pm) return;
		float pcm[2] = {left, right};
		projectm_pcm_add_float(pm, pcm, 1, PROJECTM_STEREO);
	}

	~ProjectMRenderer() { stop(); }
};

// ─── Widget ──────────────────────────────────────────────────────────────────

struct PureDreamsWidget : ModuleWidget {
	ProjectMRenderer* renderer = nullptr;
	int nvgImageId = -1;
	bool prevNextState = false;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);

		// Wide blank panel — no SVG needed, we draw everything ourselves
		box.size = Vec(PANEL_HP * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			renderer = new ProjectMRenderer();
			renderer->start();
		}

		// Controls — left strip, like Purfenator's label area
		float x = 20.f;
		addParam(createParamCentered<RoundBlackKnob>(
			Vec(x, 60.f), module, PureDreams::BRIGHTNESS_PARAM));
		addParam(createParamCentered<VCVButton>(
			Vec(x, 120.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVButton>(
			Vec(x, 160.f), module, PureDreams::NEXT_PARAM));
		addInput(createInputCentered<PJ301MPort>(
			Vec(x, 240.f), module, PureDreams::LEFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(
			Vec(x, 280.f), module, PureDreams::RIGHT_INPUT));
	}

	~PureDreamsWidget() {
		delete renderer;
	}

	void draw(const DrawArgs& args) override {
		// 1. Draw the case background (dark panel, rounded rect)
		float r = 8.f; // corner radius
		float w = box.size.x;
		float h = box.size.y;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, w, h, r);
		nvgFillColor(args.vg, nvgRGB(18, 18, 22)); // near-black case colour
		nvgFill(args.vg);

		// 2. Draw projectM frame (inset, where the visuals live)
		if (renderer && nvgImageId >= 0) {
			float margin = 36.f; // leave left strip for controls
			NVGpaint paint = nvgImagePattern(
				args.vg, margin, 0, w - margin, h, 0.f, nvgImageId, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, margin, 0, w - margin, h);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}

		// 3. Brightness overlay (black at 0 brightness, transparent at 1)
		if (renderer) {
			float brightness = renderer->brightness.load();
			float alpha = 1.f - brightness;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 36.f, 0, w - 36.f, h);
			nvgFillColor(args.vg, nvgRGBAf(0.f, 0.f, 0.f, alpha));
			nvgFill(args.vg);
		}

		// 4. Left label strip
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, 36.f, h);
		nvgFillColor(args.vg, nvgRGB(12, 12, 16));
		nvgFill(args.vg);

		// Vertical text label
		nvgSave(args.vg);
		nvgTranslate(args.vg, 12.f, h / 2.f);
		nvgRotate(args.vg, -M_PI / 2.f);
		nvgFontSize(args.vg, 11.f);
		nvgFillColor(args.vg, nvgRGB(180, 180, 200));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", NULL);
		nvgRestore(args.vg);

		ModuleWidget::draw(args);
	}

	void step() override {
		ModuleWidget& mw = *this;
		PureDreams* module = dynamic_cast<PureDreams*>(mw.module);

		if (module && renderer) {
			// Feed brightness
			renderer->brightness = module->params[PureDreams::BRIGHTNESS_PARAM].getValue();

			// Feed next/prev button triggers
			if (module->params[PureDreams::NEXT_PARAM].getValue() > 0.f)
				renderer->requestNext = true;
			if (module->params[PureDreams::PREV_PARAM].getValue() > 0.f)
				renderer->requestPrev = true;

			// Feed audio (normalise ±5V → ±1.0)
			float L = module->inputs[PureDreams::LEFT_INPUT].getVoltage() / 5.f;
			float R = module->inputs[PureDreams::RIGHT_INPUT].getVoltage() / 5.f;
			renderer->feedAudio(L, R, 1);

			// Upload latest pixel buffer as NanoVG image
			// (once GL context is wired up, uncomment below)
			// std::lock_guard<std::mutex> lock(renderer->bufferMutex);
			// if (nvgImageId < 0)
			//     nvgImageId = nvgCreateImageRGBA(args.vg, renderer->width, renderer->height, 0, renderer->pixels.data());
			// else
			//     nvgUpdateImage(args.vg, nvgImageId, renderer->pixels.data());
		}

		ModuleWidget::step();
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
