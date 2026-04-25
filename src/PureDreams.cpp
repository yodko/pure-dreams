#include "plugin.hpp"
#include "PDWindow.hpp"
#include <mutex>

static const int CTRL_HP = 3;

struct PureDreams : Module {
	enum ParamId  { PREV_PARAM, NEXT_PARAM, WIDTH_PARAM, PARAMS_LEN };
	enum InputId  { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { LIGHTS_LEN };

	int widthHP = 64;

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(WIDTH_PARAM, 16.f, 256.f, 64.f, "Width (HP)");
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "widthHP", json_integer(widthHP));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* w = json_object_get(root, "widthHP");
		if (w) widthHP = json_integer_value(w);
	}

	void process(const ProcessArgs&) override {}
};

struct PureDreamsWidget : ModuleWidget {
	PDWindow* pdWin = nullptr;
	int nvgImage = -1;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		int hp = module ? module->widthHP : 64;
		box.size = Vec(hp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin = new PDWindow();
			pdWin->open();
		}

		float cx = (CTRL_HP * RACK_GRID_WIDTH) / 2.f;
		addParam(createParamCentered<VCVBezel>(Vec(cx, RACK_GRID_HEIGHT/2.f - 30.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(Vec(cx, RACK_GRID_HEIGHT/2.f + 30.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		if (pdWin) { pdWin->close(); delete pdWin; }
		if (nvgImage >= 0) nvgDeleteImage(APP->window->vg, nvgImage);
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->params[PureDreams::NEXT_PARAM].getValue() > 0.f) pdWin->requestNext = true;
			if (m->params[PureDreams::PREV_PARAM].getValue() > 0.f) pdWin->requestPrev = true;

			// Sync width
			int hp = (int)m->params[PureDreams::WIDTH_PARAM].getValue();
			if (hp != m->widthHP) {
				m->widthHP = hp;
				box.size.x = hp * RACK_GRID_WIDTH;
			}

			// Upload pixels inside render thread output
			{
				std::lock_guard<std::mutex> lock(pdWin->pixelMutex);
				if (pdWin->pixelsDirty && !pdWin->pixels.empty()) {
					if (nvgImage >= 0) nvgDeleteImage(APP->window->vg, nvgImage);
					nvgImage = -1;
					pdWin->pixelsDirty = false;
				}
			}
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;
		float h = box.size.y;
		float cw = CTRL_HP * RACK_GRID_WIDTH;

		// Create/update NVG image inside draw() where args.vg is active
		if (pdWin && !pdWin->pixels.empty()) {
			std::lock_guard<std::mutex> lock(pdWin->pixelMutex);
			if (nvgImage < 0) {
				nvgImage = nvgCreateImageRGBA(args.vg,
					pdWin->pixelW, pdWin->pixelH, 0, pdWin->pixels.data());
			} else if (pdWin->pixelsDirty) {
				nvgDeleteImage(args.vg, nvgImage);
				nvgImage = nvgCreateImageRGBA(args.vg,
					pdWin->pixelW, pdWin->pixelH, 0, pdWin->pixels.data());
				pdWin->pixelsDirty = false;
			}
		}

		// Draw projectM as background within our own module bounds
		if (nvgImage >= 0) {
			// Y-flip: projectM renders upside down
			nvgSave(args.vg);
			nvgTranslate(args.vg, cw, h);
			nvgScale(args.vg, 1.f, -1.f);
			NVGpaint paint = nvgImagePattern(args.vg, 0, 0, w - cw, h, 0.f, nvgImage, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, w - cw, h);
			nvgFillPaint(args.vg, paint);
			nvgFill(args.vg);
			nvgRestore(args.vg);
		} else {
			// Dark background until image loads
			nvgBeginPath(args.vg);
			nvgRect(args.vg, cw, 0, w - cw, h);
			nvgFillColor(args.vg, nvgRGB(14, 14, 18));
			nvgFill(args.vg);
		}

		// Control strip
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, cw, h);
		nvgFillColor(args.vg, nvgRGB(10, 10, 14));
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, cw, 0); nvgLineTo(args.vg, cw, h);
		nvgStrokeColor(args.vg, nvgRGB(40, 40, 50));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Label
		nvgSave(args.vg);
		nvgTranslate(args.vg, cw/2.f, h/2.f);
		nvgRotate(args.vg, -M_PI/2.f);
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, nvgRGB(80, 80, 100));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);

		ModuleWidget::draw(args);
	}

	void appendContextMenu(Menu* menu) override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		for (int hp : {32, 64, 96, 128, 192, 256}) {
			menu->addChild(createMenuItem(std::to_string(hp) + " HP", "",
				[=]() {
					m->widthHP = hp;
					m->params[PureDreams::WIDTH_PARAM].setValue(hp);
					box.size.x = hp * RACK_GRID_WIDTH;
				}));
		}
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
