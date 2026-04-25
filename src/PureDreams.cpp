#include "plugin.hpp"
#include <thread>
#include <atomic>
#include <string>

#include <SDL2/SDL.h>
#include <libprojectM/projectM.hpp>

struct PureDreamsWindow {
	std::thread thread;
	std::atomic<bool> running{false};
	std::atomic<bool> requestNext{false};
	std::atomic<bool> requestPrev{false};

	void start() {
		running = true;
		thread = std::thread([this]() { loop(); });
	}

	void stop() {
		running = false;
		if (thread.joinable())
			thread.join();
	}

	void loop() {
		SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
		if (SDL_Init(SDL_INIT_VIDEO) < 0) return;

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		SDL_Window* win = SDL_CreateWindow(
			"Pure Dreams",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			1280, 720,
			SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
		if (!win) { SDL_Quit(); return; }

		SDL_GLContext glctx = SDL_GL_CreateContext(win);
		if (!glctx) { SDL_DestroyWindow(win); SDL_Quit(); return; }

		SDL_GL_SetSwapInterval(1);

		int w, h;
		SDL_GetWindowSize(win, &w, &h);

		projectM::Settings s;
		s.windowWidth  = w;
		s.windowHeight = h;
		s.fps          = 60;
		s.meshX        = 32;
		s.meshY        = 24;

		projectM* pm = new projectM(s);

		while (running) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) {
				if (e.type == SDL_QUIT) running = false;
				if (e.type == SDL_WINDOWEVENT &&
				    e.window.event == SDL_WINDOWEVENT_RESIZED) {
					SDL_GetWindowSize(win, &w, &h);
					pm->projectM_resetGL(w, h);
				}
			}

			if (requestNext.exchange(false)) pm->selectNext(true);
			if (requestPrev.exchange(false)) pm->selectPrevious(true);

			pm->renderFrame();
			SDL_GL_SwapWindow(win);
		}

		delete pm;
		SDL_GL_DeleteContext(glctx);
		SDL_DestroyWindow(win);
		SDL_Quit();
	}

	~PureDreamsWindow() { stop(); }
};

struct PureDreams : Module {
	enum ParamId { PREV_PARAM, NEXT_PARAM, PARAMS_LEN };
	enum InputId  { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { LIGHTS_LEN };

	dsp::BooleanTrigger prevTrig, nextTrig;

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
	}

	void process(const ProcessArgs&) override {}
};

struct PureDreamsWidget : ModuleWidget {
	PureDreamsWindow* pdWin = nullptr;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin = new PureDreamsWindow();
			pdWin->start();
		}

		float cx = box.size.x / 2.f;
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT / 2.f - 30.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT / 2.f + 30.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		delete pdWin;
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(10, 10, 14));
		nvgFill(args.vg);

		nvgSave(args.vg);
		nvgTranslate(args.vg, w / 2.f, h / 2.f);
		nvgRotate(args.vg, -M_PI / 2.f);
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, nvgRGB(100, 100, 120));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);

		ModuleWidget::draw(args);
	}

	void step() override {
		PureDreams* module = dynamic_cast<PureDreams*>(this->module);
		if (module && pdWin) {
			if (module->params[PureDreams::NEXT_PARAM].getValue() > 0.f)
				pdWin->requestNext = true;
			if (module->params[PureDreams::PREV_PARAM].getValue() > 0.f)
				pdWin->requestPrev = true;
		}
		ModuleWidget::step();
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
