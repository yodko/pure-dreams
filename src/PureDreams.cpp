#include "plugin.hpp"
#include "PDWindow.hpp"
#include <mutex>
#include <list>
#include <algorithm>
#include <dirent.h>

static const char* PRESET_DIR =
	"/opt/homebrew/Cellar/projectm/3.1.12/share/projectM/presets/presets_bltc201";

static std::vector<std::string>& allPresets() {
	static std::vector<std::string> ps = []() {
		std::vector<std::string> v;
		DIR* d = opendir(PRESET_DIR);
		if (!d) return v;
		struct dirent* e;
		while ((e = readdir(d))) {
			std::string n = e->d_name;
			if (n.size() > 5 && n.substr(n.size()-5) == ".milk")
				v.push_back(n.substr(0, n.size()-5));
		}
		closedir(d);
		std::sort(v.begin(), v.end());
		return v;
	}();
	return ps;
}

// ── RackBgWidget ──────────────────────────────────────────────────────────────

struct RackBgWidget : widget::Widget {
	PDWindow* pdWin;
	int   nvgImg    = -1;
	float brightness = 1.f;

	RackBgWidget(PDWindow* w) : pdWin(w) {
		box.pos  = Vec(0, 0);
		box.size = APP->scene->rack->box.size;
	}

	void step() override {
		box.size = APP->scene->rack->box.size;
		widget::Widget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;

		if (pdWin && !pdWin->pixels.empty()) {
			std::lock_guard<std::mutex> lock(pdWin->pixelMutex);
			if (pdWin->pixelsDirty) {
				if (nvgImg >= 0) nvgDeleteImage(args.vg, nvgImg);
				nvgImg = nvgCreateImageRGBA(args.vg,
					pdWin->pixelW, pdWin->pixelH, 0, pdWin->pixels.data());
				pdWin->pixelsDirty = false;
			}
		}

		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(12, 12, 16)); nvgFill(args.vg);

		if (nvgImg >= 0) {
			float cx = args.clipBox.pos.x, cy = args.clipBox.pos.y;
			float cw = args.clipBox.size.x, ch = args.clipBox.size.y;
			if (cw <= 0 || ch <= 0) { cw=w; ch=h; cx=cy=0; }
			float scale = std::max(cw/(float)pdWin->pixelW, ch/(float)pdWin->pixelH);
			float pw = pdWin->pixelW*scale, ph = pdWin->pixelH*scale;
			float ox = cx + (cw-pw)/2.f;
			nvgSave(args.vg);
			nvgTranslate(args.vg, 0, cy+ch); nvgScale(args.vg, 1, -1);
			NVGpaint p = nvgImagePattern(args.vg, ox, (ch-ph)/2.f, pw, ph,
				0, nvgImg, brightness);
			nvgBeginPath(args.vg); nvgRect(args.vg, cx, 0, cw, ch);
			nvgFillPaint(args.vg, p); nvgFill(args.vg);
			nvgRestore(args.vg);
		}
	}
};

// ── Module ────────────────────────────────────────────────────────────────────

struct PureDreams : Module {
	enum ParamId  { PREV_PARAM, NEXT_PARAM, BRIGHTNESS_PARAM, PRESET_IDX_PARAM, PARAMS_LEN };
	enum InputId  { AUDIO_INPUT, INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { LIGHTS_LEN };

	dsp::SchmittTrigger nextTrig, prevTrig;
	PDWindow* pdWin = nullptr;
	std::string savedPresetName;
	bool presetExplicitlyChanged = false; // only save when user explicitly changes

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 1.f, "Brightness");
		configParam(PRESET_IDX_PARAM, 0.f, 9999.f, 0.f, "Preset index");
		configInput(AUDIO_INPUT, "Audio");
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "presetName", json_string(savedPresetName.c_str()));
		json_object_set_new(r, "presetIdx",  json_integer((int)params[PRESET_IDX_PARAM].getValue()));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* v = json_object_get(r, "presetName");
		if (v && json_is_string(v)) savedPresetName = json_string_value(v);
	}

	void process(const ProcessArgs&) override {
		if (!pdWin) return;
		float s = inputs[AUDIO_INPUT].getVoltage() / 5.f;
		pdWin->addSample(s, s);
	}
};

// ── Preset menu item ──────────────────────────────────────────────────────────

struct PresetItem : MenuItem {
	PDWindow*   pdWin;
	PureDreams* module;
	int index;
	std::string presetName;
	ui::TextField* searchField;

	void onAction(const ActionEvent&) override {
		if (pdWin) pdWin->requestPreset = index;
		if (module) {
			module->savedPresetName = presetName;
			module->presetExplicitlyChanged = true;
			APP->engine->setParamValue(module, PureDreams::PRESET_IDX_PARAM, (float)index);
		}
	}

	void step() override {
		if (searchField) {
			std::string q = searchField->text, n = presetName;
			std::transform(q.begin(), q.end(), q.begin(), ::tolower);
			std::transform(n.begin(), n.end(), n.begin(), ::tolower);
			visible = q.empty() || n.find(q) != std::string::npos;
		}
		MenuItem::step();
	}
};

// ── Binary LED preset display ─────────────────────────────────────────────────

struct BinaryLEDDisplay : Widget {
	PDWindow* pdWin  = nullptr;
	int       total  = 413;

	BinaryLEDDisplay() { box.size = Vec(22, 22); }

	void draw(const DrawArgs& args) override {
		int idx = pdWin ? pdWin->currentPresetIndex.load() : 0;
		// 9 bits, 3x3 grid. Bit 8 (MSB) top-left, bit 0 (LSB) bottom-right
		for (int bit = 0; bit < 9; bit++) {
			bool lit = (idx >> (8 - bit)) & 1;
			int row = bit / 3, col = bit % 3;
			float x = col * 7.f + 3.5f;
			float y = row * 7.f + 3.5f;
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, y, 2.8f);
			nvgFillColor(args.vg, lit ? nvgRGB(80,200,80) : nvgRGB(140,140,130));
			nvgFill(args.vg);
		}
	}

	void onHover(const HoverEvent& e) override { e.consume(this); }

	ui::Tooltip* createTooltip() {
		auto* t = new ui::Tooltip;
		int idx = pdWin ? pdWin->currentPresetIndex.load() : 0;
		t->text = string::f("Preset %d / %d", idx + 1, total);
		return t;
	}
};

// ── Widget ────────────────────────────────────────────────────────────────────

struct PureDreamsWidget : ModuleWidget {
	PDWindow*     pdWin    = nullptr;
	RackBgWidget* rackBg   = nullptr;
	bool          restored = false;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin  = new PDWindow();
			pdWin->open();
			rackBg = new RackBgWidget(pdWin);
			auto& ch = APP->scene->rack->children;
			auto it  = ch.begin();
			if (!ch.empty()) std::advance(it, 1);
			ch.insert(it, rackBg);
			rackBg->parent = APP->scene->rack;
			module->pdWin = pdWin;
		}

		float cx = box.size.x / 2.f;
		// Buttons side by side
		addParam(createParamCentered<TL1105>(Vec(8.f,  55.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<TL1105>(Vec(22.f, 55.f), module, PureDreams::NEXT_PARAM));
		// Brightness knob
		addParam(createParamCentered<Trimpot>(Vec(cx, 160.f), module, PureDreams::BRIGHTNESS_PARAM));
		// Audio input
		addInput(createInputCentered<PJ301MPort>(Vec(cx, RACK_GRID_HEIGHT - 50.f), module, PureDreams::AUDIO_INPUT));

		// Binary LED display (9-bit preset indicator)
		if (module) {
			auto* led = createWidget<BinaryLEDDisplay>(Vec(4.f, 85.f));
			led->pdWin = pdWin;
			led->total = (int)allPresets().size();
			addChild(led);
		}
	}

	~PureDreamsWidget() {
		// Null module's pdWin FIRST — audio thread checks this in process()
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m) m->pdWin = nullptr;

		if (rackBg) {
			if (APP && APP->scene && APP->scene->rack)
				APP->scene->rack->removeChild(rackBg);
			delete rackBg; rackBg = nullptr;
		}

		// Close render thread, then defer delete to after engine removes module
		// (Engine::removeModule runs in ModuleWidget::~ModuleWidget after our body)
		if (pdWin) {
			pdWin->close();
			PDWindow* pw = pdWin;
			pdWin = nullptr;
			dispatch_async(dispatch_get_main_queue(), ^{ delete pw; });
		}
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->nextTrig.process(m->params[PureDreams::NEXT_PARAM].getValue()) > 0.f) {
				pdWin->requestNext = true;
				m->presetExplicitlyChanged = true;
			}
			if (m->prevTrig.process(m->params[PureDreams::PREV_PARAM].getValue()) > 0.f) {
				pdWin->requestPrev = true;
				m->presetExplicitlyChanged = true;
			}
			if (rackBg)
				rackBg->brightness = m->params[PureDreams::BRIGHTNESS_PARAM].getValue();

			// Restore saved preset once projectM is ready
			if (!restored && pdWin->pixelsDirty) {
				restored = true;
				if (!m->savedPresetName.empty()) {
					auto& ps = allPresets();
					auto it = std::find(ps.begin(), ps.end(), m->savedPresetName);
					if (it != ps.end()) {
						pdWin->requestPreset = (int)std::distance(ps.begin(), it);
					}
				}
			}

			// Only save preset name when user explicitly changed it (not on restore)
			if (m->presetExplicitlyChanged) {
				std::lock_guard<std::mutex> nl(pdWin->nameMutex);
				if (!pdWin->currentPresetName.empty()) {
					m->savedPresetName = pdWin->currentPresetName;
					int idx = pdWin->currentPresetIndex.load();
					APP->engine->setParamValue(m, PureDreams::PRESET_IDX_PARAM, (float)idx);
					m->presetExplicitlyChanged = false;
				}
			}
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;

		// Off-white panel like FM-OP
		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(228, 228, 220)); nvgFill(args.vg);

		// Border
		nvgBeginPath(args.vg); nvgRect(args.vg, 0.5f, 0.5f, w-1, h-1);
		nvgStrokeColor(args.vg, nvgRGB(160,160,155));
		nvgStrokeWidth(args.vg, 1.f); nvgStroke(args.vg);

		// Phillips screws
		auto drawScrew = [&](float x, float y) {
			nvgBeginPath(args.vg); nvgCircle(args.vg, x, y, 4.f);
			NVGpaint sp = nvgRadialGradient(args.vg, x-1, y-1, 0.5f, 4.f,
				nvgRGB(215,215,208), nvgRGB(185,185,178));
			nvgFillPaint(args.vg, sp); nvgFill(args.vg);
			nvgBeginPath(args.vg); nvgCircle(args.vg, x, y, 4.f);
			nvgStrokeColor(args.vg, nvgRGB(155,155,148));
			nvgStrokeWidth(args.vg, 0.7f); nvgStroke(args.vg);
			// Phillips cross
			float r = 2.2f;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x-r, y); nvgLineTo(args.vg, x+r, y);
			nvgMoveTo(args.vg, x, y-r); nvgLineTo(args.vg, x, y+r);
			nvgStrokeColor(args.vg, nvgRGBA(0,0,0,70));
			nvgStrokeWidth(args.vg, 1.1f); nvgStroke(args.vg);
		};
		drawScrew(w/2.f, 8.f);
		drawScrew(w/2.f, h-8.f);

		nvgFontSize(args.vg, 6.5f);
		nvgFillColor(args.vg, nvgRGB(70,70,65));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, w/2.f, 22.f, "PURE", nullptr);
		nvgText(args.vg, w/2.f, 30.f, "DREAMS", nullptr);

		// Button labels
		nvgFontSize(args.vg, 7.f);
		nvgFillColor(args.vg, nvgRGB(90,90,85));
		nvgText(args.vg,  8.f, 67.f, "-", nullptr);
		nvgText(args.vg, 22.f, 67.f, "+", nullptr);

		// Brightness label
		nvgFontSize(args.vg, 6.f);
		nvgFillColor(args.vg, nvgRGB(100,100,95));
		nvgText(args.vg, w/2.f, 173.f, "DIM", nullptr);

		// IN label
		nvgText(args.vg, w/2.f, RACK_GRID_HEIGHT - 68.f, "IN", nullptr);

		ModuleWidget::draw(args);
	}

	void appendContextMenu(Menu* menu) override {
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Presets"));

		auto* search = createWidget<ui::TextField>(Vec(0,0));
		search->placeholder = "search…";
		search->box.size.x  = 200;
		menu->addChild(search);

		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		auto& ps = allPresets();
		int cur = pdWin ? pdWin->currentPresetIndex.load() : -1;
		for (int i = 0; i < (int)ps.size(); i++) {
			auto* item        = new PresetItem;
			item->text        = (i == cur ? "► " : "  ") + ps[i];
			item->pdWin       = pdWin;
			item->module      = m;
			item->index       = i;
			item->presetName  = ps[i];
			item->searchField = search;
			menu->addChild(item);
		}
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
