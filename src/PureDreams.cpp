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

		// Always fill dark base first
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
	std::string savedPresetName; // saved by name for reliability

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 1.f, "Brightness");
		configInput(AUDIO_INPUT, "Audio");
		configParam(PRESET_IDX_PARAM, 0.f, 9999.f, 0.f, "Preset index");
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "presetName", json_string(savedPresetName.c_str()));
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

// ── Searchable preset menu item ───────────────────────────────────────────────

struct PresetItem : MenuItem {
	PDWindow*   pdWin;
	PureDreams* module;
	int index;
	std::string presetName;
	ui::TextField* searchField;

	void onAction(const ActionEvent&) override {
		if (pdWin)   pdWin->requestPreset = index;
		if (module)  module->savedPresetName = presetName;
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
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f - 50.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f - 10.f), module, PureDreams::NEXT_PARAM));
		addParam(createParamCentered<Trimpot>(
			Vec(cx, RACK_GRID_HEIGHT/2.f + 45.f), module, PureDreams::BRIGHTNESS_PARAM));
		addInput(createInputCentered<PJ301MPort>(
			Vec(cx, RACK_GRID_HEIGHT - 30.f), module, PureDreams::AUDIO_INPUT));
	}

	~PureDreamsWidget() {
		if (rackBg) {
			if (APP && APP->scene && APP->scene->rack)
				APP->scene->rack->removeChild(rackBg);
			delete rackBg; rackBg = nullptr;
		}
		if (pdWin) { pdWin->close(); delete pdWin; pdWin = nullptr; }
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->nextTrig.process(m->params[PureDreams::NEXT_PARAM].getValue()) > 0.f) {
				pdWin->requestNext = true;
			}
			if (m->prevTrig.process(m->params[PureDreams::PREV_PARAM].getValue()) > 0.f) {
				pdWin->requestPrev = true;
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
						int idx = (int)std::distance(ps.begin(), it);
						pdWin->requestPreset = idx;
					}
				}
			}

			// Sync savedPresetName + hidden PRESET_IDX_PARAM (triggers dirty flag)
			{
				std::lock_guard<std::mutex> nl(pdWin->nameMutex);
				if (!pdWin->currentPresetName.empty() &&
				    pdWin->currentPresetName != m->savedPresetName) {
					m->savedPresetName = pdWin->currentPresetName;
					// Update hidden param so VCV detects the change
					int idx = pdWin->currentPresetIndex.load();
					float cur = m->params[PureDreams::PRESET_IDX_PARAM].getValue();
					if ((int)cur != idx)
						APP->engine->setParamValue(m, PureDreams::PRESET_IDX_PARAM, (float)idx);
				}
			}
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(10,10,14)); nvgFill(args.vg);

		PureDreams* m = dynamic_cast<PureDreams*>(this->module);

		// Preset counter: "042 / 4188"
		if (pdWin && m) {
			int idx   = pdWin->currentPresetIndex.load();
			int total = (int)allPresets().size();
			char buf[32];
			snprintf(buf, sizeof(buf), "%d / %d", idx + 1, total);
			nvgFontSize(args.vg, 8.f);
			nvgFillColor(args.vg, nvgRGB(120, 120, 150));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
			nvgText(args.vg, w/2.f, h/2.f + 22.f, buf, nullptr);
		}

		// Current preset name (rotated)
		if (pdWin) {
			std::lock_guard<std::mutex> nl(pdWin->nameMutex);
			if (!pdWin->currentPresetName.empty()) {
				std::string name = pdWin->currentPresetName;
				if (name.size() > 16) name = name.substr(0,15) + "…";
				nvgSave(args.vg);
				nvgTranslate(args.vg, w/2.f, h/2.f + 75.f);
				nvgRotate(args.vg, -M_PI/2.f);
				nvgFontSize(args.vg, 7.f);
				nvgFillColor(args.vg, nvgRGB(90, 90, 115));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
				nvgText(args.vg, 0, 0, name.c_str(), nullptr);
				nvgRestore(args.vg);
			}
		}

		nvgSave(args.vg);
		nvgTranslate(args.vg, w/2.f, h/2.f - 80.f);
		nvgRotate(args.vg, -M_PI/2.f);
		nvgFontSize(args.vg, 8.f); nvgFillColor(args.vg, nvgRGB(80,80,100));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);

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
