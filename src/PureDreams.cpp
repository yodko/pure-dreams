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

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		if (module) { pdWin = new PDWindow(); pdWin->open(); }
		float cx = box.size.x / 2.f;
		addParam(createParamCentered<VCVBezel>(Vec(cx, RACK_GRID_HEIGHT/2.f - 30.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(Vec(cx, RACK_GRID_HEIGHT/2.f + 30.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		if (pdWin) { pdWin->close(); delete pdWin; }
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(10, 10, 14)); nvgFill(args.vg);
		nvgSave(args.vg);
		nvgTranslate(args.vg, w/2.f, h/2.f); nvgRotate(args.vg, -M_PI/2.f);
		nvgFontSize(args.vg, 8.f); nvgFillColor(args.vg, nvgRGB(100, 100, 120));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);
		ModuleWidget::draw(args);
	}

	void step() override {
		PureDreams* module = dynamic_cast<PureDreams*>(this->module);
		if (module && pdWin) {
			if (module->params[PureDreams::NEXT_PARAM].getValue() > 0.f) pdWin->requestNext = true;
			if (module->params[PureDreams::PREV_PARAM].getValue() > 0.f) pdWin->requestPrev = true;
		}
		ModuleWidget::step();
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
