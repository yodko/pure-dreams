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
	enum ParamId  { PREV_PARAM, NEXT_PARAM, BRIGHTNESS_PARAM, SMOOTH_PARAM, PRESET_IDX_PARAM, PARAMS_LEN };
	enum InputId  { AUDIO_INPUT, INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { BIT_LIGHTS, SMOOTH_LIGHT = BIT_LIGHTS + 9, LIGHTS_LEN };

	dsp::SchmittTrigger nextTrig, prevTrig;
	PDWindow* pdWin = nullptr;
	std::string savedPresetName;
	bool presetExplicitlyChanged = false;
	float labelSize = 10.f;
	float smoothed  = 0.f;
	float envelope  = 0.f; // peak envelope for LED flicker display

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 1.f, "Brightness");
		configParam(SMOOTH_PARAM, 0.f, 1.f, 0.f, "Smoothing");
		configParam(PRESET_IDX_PARAM, 0.f, 9999.f, 0.f, "Preset index");
		configInput(AUDIO_INPUT, "Audio");
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "presetName", json_string(savedPresetName.c_str()));
		json_object_set_new(r, "presetIdx",  json_integer((int)params[PRESET_IDX_PARAM].getValue()));
		json_object_set_new(r, "labelSize",  json_real(labelSize));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* v = json_object_get(r, "presetName");
		if (v && json_is_string(v)) savedPresetName = json_string_value(v);
		json_t* ls = json_object_get(r, "labelSize");
		if (ls) labelSize = (float)json_real_value(ls);
	}

	void process(const ProcessArgs&) override {
		if (!pdWin) return;
		float raw = inputs[AUDIO_INPUT].getVoltage() / 5.f;
		float s   = params[SMOOTH_PARAM].getValue();

		// IIR low-pass for projectM audio feed
		float coeff = s > 0.001f ? 1.f - std::pow(10.f, -s * 4.f) : 0.f;
		smoothed    = smoothed * coeff + raw * (1.f - coeff);
		pdWin->addSample(smoothed, smoothed);

		// Envelope follower for LED: instant attack, variable release
		// s=0: release ~0.7ms → fast flicker (tracks individual audio peaks)
		// s=1: release ~700ms → 1-2 flickers/sec
		float absRaw   = std::abs(raw);
		float relCoeff = 1.f - std::pow(10.f, -1.5f - s * 3.f);
		if (absRaw >= envelope)
			envelope = absRaw;
		else
			envelope *= relCoeff;
		lights[SMOOTH_LIGHT].setBrightness(envelope * 2.f);
		// Drive binary LED lights from current preset index
		int idx = pdWin->currentPresetIndex.load();
		for (int i = 0; i < 9; i++)
			lights[BIT_LIGHTS + i].setBrightness((idx >> (8 - i)) & 1 ? 0.45f : 0.f);
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


// ── Float quantity for right-click sliders ────────────────────────────────────

struct FloatQ : Quantity {
	float* v; float lo, hi; std::string lbl;
	FloatQ(float* v, float lo, float hi, const std::string& l) : v(v), lo(lo), hi(hi), lbl(l) {}
	void setValue(float x) override { *v = clamp(x, lo, hi); }
	float getValue() override { return *v; }
	float getMinValue() override { return lo; }
	float getMaxValue() override { return hi; }
	float getDefaultValue() override { return lo; }
	std::string getLabel() override { return lbl; }
	std::string getDisplayValueString() override { return string::f("%.1f", *v); }
};

// ── Transparent hover widget for LED tooltip ─────────────────────────────────

struct LEDTooltipArea : Widget {
	PDWindow* pdWin  = nullptr;
	int       total  = 413;

	LEDTooltipArea() { box.size = Vec(26, 26); }
	void onHover(const HoverEvent& e) override { e.consume(this); }
	ui::Tooltip* createTooltip() {
		auto* t = new ui::Tooltip;
		int idx = pdWin ? pdWin->currentPresetIndex.load() : 0;
		t->text = string::f("%d / %d", idx + 1, total);
		return t;
	}
};

// ── Widget ────────────────────────────────────────────────────────────────────

struct PureDreamsWidget : ModuleWidget {
	PDWindow*     pdWin         = nullptr;
	RackBgWidget* rackBg        = nullptr;
	bool          restored      = false;
	int           saveDelay     = 0;  // frames to wait after preset change before saving

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
		// Binary LEDs under title (rows at y=35,42,49)
		for (int i = 0; i < 9; i++) {
			int row = i / 3, col = i % 3;
			addChild(createLight<SmallLight<GreenLight>>(
				Vec(5.f + col*7.f, 35.f + row*7.f), module, PureDreams::BIT_LIGHTS + i));
		}
		if (module) {
			auto* tip = createWidget<LEDTooltipArea>(Vec(2.f, 32.f));
			tip->pdWin = pdWin;
			tip->total = (int)allPresets().size();
			addChild(tip);
		}
		// Buttons below LEDs
		addParam(createParamCentered<TL1105>(Vec(cx, 82.f), module, PureDreams::NEXT_PARAM));
		addParam(createParamCentered<TL1105>(Vec(cx, 108.f), module, PureDreams::PREV_PARAM));
		// Brightness knob
		addParam(createParamCentered<Trimpot>(Vec(cx, 152.f), module, PureDreams::BRIGHTNESS_PARAM));
		// Smoothing slider + flicker light showing smoothed signal
		addParam(createParamCentered<VCVSlider>(Vec(cx, 230.f), module, PureDreams::SMOOTH_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(cx + 10.f, 177.f), module, PureDreams::SMOOTH_LIGHT));
		// Audio input
		addInput(createInputCentered<PJ301MPort>(Vec(cx, RACK_GRID_HEIGHT - 50.f), module, PureDreams::AUDIO_INPUT));
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

		if (pdWin) { pdWin->close(); delete pdWin; pdWin = nullptr; }
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->nextTrig.process(m->params[PureDreams::NEXT_PARAM].getValue()) > 0.f) {
				pdWin->requestNext = true;
				saveDelay = 30; // wait 30 frames for projectM to update name
			}
			if (m->prevTrig.process(m->params[PureDreams::PREV_PARAM].getValue()) > 0.f) {
				pdWin->requestPrev = true;
				saveDelay = 30;
			}
			if (rackBg)
				rackBg->brightness = m->params[PureDreams::BRIGHTNESS_PARAM].getValue();

			// Restore saved preset once projectM is ready; default to preset 0
			if (!restored && pdWin->pixelsDirty) {
				restored = true;
				if (!m->savedPresetName.empty()) {
					auto& ps = allPresets();
					auto it = std::find(ps.begin(), ps.end(), m->savedPresetName);
					if (it != ps.end())
						pdWin->requestPreset = (int)std::distance(ps.begin(), it);
					else
						pdWin->requestPreset = 0; // fallback to first
				} else {
					pdWin->requestPreset = 0; // fresh placement → first preset
				}
			}

			// After delay, save the current preset name (render thread has had time to update it)
			if (saveDelay > 0) {
				if (--saveDelay == 0) {
					std::lock_guard<std::mutex> nl(pdWin->nameMutex);
					if (!pdWin->currentPresetName.empty()) {
						m->savedPresetName = pdWin->currentPresetName;
						int idx = pdWin->currentPresetIndex.load();
						APP->engine->setParamValue(m, PureDreams::PRESET_IDX_PARAM, (float)idx);
					}
				}
			}

			// Also save on menu selection (presetExplicitlyChanged set by PresetItem)
			if (m->presetExplicitlyChanged) {
				m->presetExplicitlyChanged = false;
				saveDelay = 30;
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

		// Bold helpers — slight offset = faux bold
		auto boldText = [&](float x, float y, const char* t) {
			nvgText(args.vg, x,       y, t, nullptr);
			nvgText(args.vg, x+0.4f,  y, t, nullptr); // normal bold
		};
		auto thinText = [&](float x, float y, const char* t) {
			nvgText(args.vg, x,       y, t, nullptr);
			nvgText(args.vg, x+0.2f,  y, t, nullptr); // half bold for + and -
		};

		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);

		// Title: ('-')
		nvgFontSize(args.vg, 11.f);
		nvgFillColor(args.vg, nvgRGB(28,28,22));
		boldText(w/2.f, 22.f, "('-')");

		// + above top button, - below bottom button (thinner than rest)
		nvgFontSize(args.vg, 11.f);
		nvgFillColor(args.vg, nvgRGB(32,32,26));
		thinText(w/2.f, 68.f, "+");
		thinText(w/2.f, 122.f, "–");

		// Brightness label
		nvgFontSize(args.vg, 9.f);
		nvgFillColor(args.vg, nvgRGB(45,45,38));
		boldText(w/2.f, 167.f, "DIM");

		// Smooth label above slider
		nvgFontSize(args.vg, 7.f);
		nvgFillColor(args.vg, nvgRGB(55,55,48));
		boldText(w/2.f, 176.f, "SMOOTH");

		// Socket surround — elongated to include IN inside
		{
			float sx = w/2.f, sy = RACK_GRID_HEIGHT - 50.f;
			float sw2 = 13.5f, sh2 = 18.f; // wider vertically to fit IN
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, sx-sw2, sy-sw2, sw2*2, sh2*2, 5.f);
			nvgFillColor(args.vg, nvgRGB(212,212,206));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, sx-sw2, sy-sw2, sw2*2, sh2*2, 5.f);
			nvgStrokeColor(args.vg, nvgRGB(175,175,170));
			nvgStrokeWidth(args.vg, 1.f); nvgStroke(args.vg);
			// Bold IN inside the box
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGB(30,30,25));
			boldText(sx, sy + sw2 + 5.f, "IN");
		}

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
