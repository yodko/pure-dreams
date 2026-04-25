#include "plugin.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

#include <libprojectM/projectM.hpp>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

static const int PANEL_HP = 4;
static const float CASE_W  = 2600.f;
static const float CASE_H  = RACK_GRID_HEIGHT;

struct PureDreams : Module {
	enum ParamId {
		BRIGHTNESS_PARAM,
		PREV_PARAM,
		NEXT_PARAM,
		PARAMS_LEN
	};
	enum InputId { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 0.8f, "Brightness");
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
	}

	void process(const ProcessArgs&) override {}
};

struct ProjectMRenderer {
	std::thread renderThread;
	std::mutex bufferMutex;
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};
	std::atomic<float> brightness{0.8f};

	int width = 1280;
	int height = 128;
	std::vector<uint8_t> pixels;

	projectM* pm = nullptr;

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

	CGLContextObj createContext() {
		CGLPixelFormatAttribute attrs[] = {
			kCGLPFAAccelerated,
			kCGLPFAColorSize,     (CGLPixelFormatAttribute)24,
			kCGLPFADepthSize,     (CGLPixelFormatAttribute)16,
			kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
			(CGLPixelFormatAttribute)0
		};
		CGLPixelFormatObj pix;
		GLint num;
		CGLChoosePixelFormat(attrs, &pix, &num);
		CGLContextObj ctx;
		CGLCreateContext(pix, nullptr, &ctx);
		CGLDestroyPixelFormat(pix);
		return ctx;
	}

	void loop() {
		CGLContextObj ctx = createContext();
		if (!ctx) return;
		CGLSetCurrentContext(ctx);

		projectM::Settings s;
		s.windowWidth  = width;
		s.windowHeight = height;
		s.fps          = 60;
		s.meshX        = 32;
		s.meshY        = 24;
		pm = new projectM(s);

		while (running) {
			if (requestNext.exchange(false)) pm->selectNext(true);
			if (requestPrev.exchange(false)) pm->selectPrevious(true);

			pm->renderFrame();

			{
				std::lock_guard<std::mutex> lock(bufferMutex);
				glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			}
		}

		delete pm;
		pm = nullptr;
		CGLSetCurrentContext(nullptr);
		CGLDestroyContext(ctx);
	}

	void feedAudio(float left, float right) {
		if (!pm) return;
		float pcm[2] = {left, right};
		pm->pcm()->addPCMfloat(pcm, 2);
	}

	~ProjectMRenderer() { stop(); }
};

struct PureDreamsWidget : ModuleWidget {
	ProjectMRenderer* renderer = nullptr;
	int nvgImageId = -1;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(PANEL_HP * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			renderer = new ProjectMRenderer();
			renderer->start();
		}

		float cx = box.size.x / 2.f;
		addParam(createParamCentered<Trimpot>(
			Vec(cx, 80.f), module, PureDreams::BRIGHTNESS_PARAM));
		addParam(createParamCentered<TL1105>(
			Vec(cx, 140.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<TL1105>(
			Vec(cx, 200.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		delete renderer;
	}

	void draw(const DrawArgs& args) override {
		float pw = box.size.x;
		float h  = CASE_H;
		float cw = CASE_W;

		// case background — extends far to the right behind other modules
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, cw, h, 6.f);
		nvgFillColor(args.vg, nvgRGB(14, 14, 18));
		nvgFill(args.vg);

		// case border
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, cw, h, 6.f);
		nvgStrokeColor(args.vg, nvgRGB(50, 50, 60));
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);

		// projectM texture
		if (renderer && nvgImageId >= 0) {
			float brightness = renderer->brightness.load();
			NVGpaint paint = nvgImagePattern(
				args.vg, pw, 0, cw - pw, h, 0.f, nvgImageId, brightness);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, pw, 0, cw - pw, h);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
		}

		// control strip (our actual narrow panel)
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, pw, h);
		nvgFillColor(args.vg, nvgRGB(10, 10, 14));
		nvgFill(args.vg);

		// right edge of control strip separator line
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, pw, 0);
		nvgLineTo(args.vg, pw, h);
		nvgStrokeColor(args.vg, nvgRGB(50, 50, 60));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// vertical label
		nvgSave(args.vg);
		nvgTranslate(args.vg, pw / 2.f, h / 2.f);
		nvgRotate(args.vg, -M_PI / 2.f);
		nvgFontSize(args.vg, 9.f);
		nvgFillColor(args.vg, nvgRGB(120, 120, 140));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);

		ModuleWidget::draw(args);
	}

	void step() override {
		PureDreams* module = dynamic_cast<PureDreams*>(this->module);

		if (module && renderer) {
			renderer->brightness = module->params[PureDreams::BRIGHTNESS_PARAM].getValue();

			if (module->params[PureDreams::NEXT_PARAM].getValue() > 0.f)
				renderer->requestNext = true;
			if (module->params[PureDreams::PREV_PARAM].getValue() > 0.f)
				renderer->requestPrev = true;

			{
				std::lock_guard<std::mutex> lock(renderer->bufferMutex);
				if (!renderer->pixels.empty()) {
					if (nvgImageId < 0)
						nvgImageId = nvgCreateImageRGBA(
							APP->window->vg,
							renderer->width, renderer->height,
							NVG_IMAGE_FLIPY,
							renderer->pixels.data());
					else
						nvgUpdateImage(
							APP->window->vg,
							nvgImageId,
							renderer->pixels.data());
				}
			}
		}

		ModuleWidget::step();
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
