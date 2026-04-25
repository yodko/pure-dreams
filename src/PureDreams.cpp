#include "plugin.hpp"
#include "PDWindow.hpp"

struct PureDreams : Module {
	enum ParamId  { PREV_PARAM, NEXT_PARAM, PARAMS_LEN };
	enum InputId  { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { LIGHTS_LEN };

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
	}
	void process(const ProcessArgs&) override {}
};

struct PureDreamsWidget : ModuleWidget {
	PDWindow* pdWin = nullptr;
	int nvgImage = -1;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin = new PDWindow();
			pdWin->open();
		}

		float cx = box.size.x / 2.f;
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT / 2.f - 30.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT / 2.f + 30.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		if (pdWin) { pdWin->close(); delete pdWin; }
		if (nvgImage >= 0) nvgDeleteImage(APP->window->vg, nvgImage);
	}

	void step() override {
		PureDreams* module = dynamic_cast<PureDreams*>(this->module);
		if (module && pdWin) {
			if (module->params[PureDreams::NEXT_PARAM].getValue() > 0.f)
				pdWin->requestNext = true;
			if (module->params[PureDreams::PREV_PARAM].getValue() > 0.f)
				pdWin->requestPrev = true;

			std::lock_guard<std::mutex> lock(pdWin->pixelMutex);
			if (pdWin->pixelsDirty && !pdWin->pixels.empty()) {
				if (nvgImage < 0)
					nvgImage = nvgCreateImageRGBA(APP->window->vg,
						pdWin->pixelW, pdWin->pixelH,
						NVG_IMAGE_FLIPY, pdWin->pixels.data());
				else
					nvgUpdateImage(APP->window->vg, nvgImage, pdWin->pixels.data());
				pdWin->pixelsDirty = false;
			}
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		// Break out of our module's scissor rect and paint projectM
		// across the entire rack background
		if (nvgImage >= 0) {
			nvgSave(args.vg);
			nvgResetScissor(args.vg);

			// Our module is at box.pos in rack space.
			// (-box.pos.x, -box.pos.y) puts us at the rack origin.
			float rx = -box.pos.x;
			float ry = -box.pos.y;
			float rw = 16000.f;
			float rh = 4000.f;

			NVGpaint paint = nvgImagePattern(
				args.vg, rx, ry, rw, rh, 0.f, nvgImage, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, rx, ry, rw, rh);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);

			nvgRestore(args.vg);
		}

		// Our narrow panel
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBA(10, 10, 14, 200));
		nvgFill(args.vg);

		nvgSave(args.vg);
		nvgTranslate(args.vg, box.size.x / 2.f, box.size.y / 2.f);
		nvgRotate(args.vg, -M_PI / 2.f);
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, nvgRGB(100, 100, 120));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);

		ModuleWidget::draw(args);
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
