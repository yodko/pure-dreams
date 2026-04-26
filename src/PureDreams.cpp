#include "plugin.hpp"
#include "PDWindow.hpp"
#include <mutex>
#include <list>
#include <algorithm>
#include <dirent.h>
#include <string>
#include <vector>

static std::string PRESET_DIR =
	"/opt/homebrew/Cellar/projectm/3.1.12/share/projectM/presets";

static std::vector<std::string> getPresetNames() {
	std::vector<std::string> names;
	DIR* d = opendir(PRESET_DIR.c_str());
	if (!d) return names;
	struct dirent* e;
	while ((e = readdir(d))) {
		std::string n = e->d_name;
		if (n.size() > 5 && n.substr(n.size()-5) == ".milk") {
			names.push_back(n.substr(0, n.size()-5));
		}
	}
	closedir(d);
	std::sort(names.begin(), names.end());
	return names;
}

// ── RackBgWidget ──────────────────────────────────────────────────────────────

struct RackBgWidget : widget::Widget {
	PDWindow* pdWin;
	int nvgImg = -1;

	// Case appearance
	NVGcolor caseColor     = nvgRGB(18, 18, 22);
	NVGcolor railColor     = nvgRGB(30, 30, 36);
	NVGcolor borderColor   = nvgRGB(60, 60, 72);
	float    railH         = 18.f;
	float    pad           = 14.f;
	float    brightness    = 0.85f;

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

		// ── projectM background ────────────────────────────────────────────
		if (pdWin && !pdWin->pixels.empty()) {
			std::lock_guard<std::mutex> lock(pdWin->pixelMutex);
			if (pdWin->pixelsDirty) {
				if (nvgImg >= 0) nvgDeleteImage(args.vg, nvgImg);
				nvgImg = nvgCreateImageRGBA(args.vg,
					pdWin->pixelW, pdWin->pixelH, 0, pdWin->pixels.data());
				pdWin->pixelsDirty = false;
			}
		}


		if (nvgImg >= 0) {
			float cx = args.clipBox.pos.x, cy = args.clipBox.pos.y;
			float cw = args.clipBox.size.x, ch = args.clipBox.size.y;
			if (cw <= 0 || ch <= 0) { cw = w; ch = h; cx = cy = 0; }
			float imgW = pdWin->pixelW, imgH = pdWin->pixelH;
			float scale = std::max(cw / imgW, ch / imgH);
			float pw = imgW * scale, ph = imgH * scale;
			float ox = cx + (cw - pw) / 2.f;
			nvgSave(args.vg);
			nvgTranslate(args.vg, 0, cy + ch);
			nvgScale(args.vg, 1.f, -1.f);
			NVGpaint p = nvgImagePattern(args.vg, ox, (ch-ph)/2.f, pw, ph, 0, nvgImg, this->brightness);
			nvgBeginPath(args.vg); nvgRect(args.vg, cx, 0, cw, ch);
			nvgFillPaint(args.vg, p); nvgFill(args.vg);
			nvgRestore(args.vg);
		} else {
			nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
			nvgFillColor(args.vg, nvgRGB(12, 12, 16)); nvgFill(args.vg);
		}

		// ── Eurorack case frame ────────────────────────────────────────────
		auto& rchildren = APP->scene->rack->children;
		widget::Widget* mc = nullptr;
		if (rchildren.size() >= 3) {
			auto it = rchildren.begin();
			std::advance(it, 2);
			mc = *it;
		}
		if (mc && !mc->children.empty()) {
			float x1=1e9, y1=1e9, x2=-1e9, y2=-1e9;
			for (auto* c : mc->children) {
				x1 = std::min(x1, c->box.pos.x);
				y1 = std::min(y1, c->box.pos.y);
				x2 = std::max(x2, c->box.pos.x + c->box.size.x);
				y2 = std::max(y2, c->box.pos.y + c->box.size.y);
			}
			x1-=pad; y1-=pad-4; x2+=pad; y2+=pad-4;
			float fw = x2-x1, fh = y2-y1;

			// Drop shadow (layered for soft look)
			for (int i = 3; i >= 1; i--) {
				float off = i * 6.f;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x1+off, y1+off, fw, fh, 8.f);
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, (int)(60 / i)));
				nvgFill(args.vg);
			}

			// Top rail
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, railH, 6.f);
			nvgFillColor(args.vg, nvgRGBAf(railColor.r, railColor.g, railColor.b, 0.92f));
			nvgFill(args.vg);

			// Bottom rail
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y2-railH, fw, railH, 6.f);
			nvgFillColor(args.vg, nvgRGBAf(railColor.r, railColor.g, railColor.b, 0.92f));
			nvgFill(args.vg);

			// Left strip
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x1, y1+railH, 6.f, fh-railH*2);
			nvgFillColor(args.vg, nvgRGBAf(caseColor.r, caseColor.g, caseColor.b, 0.92f));
			nvgFill(args.vg);

			// Right strip
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x2-6.f, y1+railH, 6.f, fh-railH*2);
			nvgFillColor(args.vg, nvgRGBAf(caseColor.r, caseColor.g, caseColor.b, 0.92f));
			nvgFill(args.vg);

			// Outer border
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, fh, 8.f);
			nvgStrokeColor(args.vg, borderColor);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
	}
};

// ── Module ────────────────────────────────────────────────────────────────────

struct PureDreams : Module {
	enum ParamId  { PREV_PARAM, NEXT_PARAM, BRIGHTNESS_PARAM, PARAMS_LEN };
	enum InputId  { INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId  { LIGHTS_LEN };

	dsp::SchmittTrigger prevTrig, nextTrig;
	std::atomic<bool> requestNext{false}, requestPrev{false};

	int caseR=18, caseG=18, caseB=22;
	int railR=30, railG=30, railB=36;

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 0.85f, "Brightness");
	}

	void process(const ProcessArgs&) override {
		if (nextTrig.process(params[NEXT_PARAM].getValue())) requestNext = true;
		if (prevTrig.process(params[PREV_PARAM].getValue())) requestPrev = true;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "caseR", json_integer(caseR));
		json_object_set_new(r, "caseG", json_integer(caseG));
		json_object_set_new(r, "caseB", json_integer(caseB));
		json_object_set_new(r, "railR", json_integer(railR));
		json_object_set_new(r, "railG", json_integer(railG));
		json_object_set_new(r, "railB", json_integer(railB));
		return r;
	}
	void dataFromJson(json_t* r) override {
		auto gi = [&](const char* k, int def) {
			json_t* v = json_object_get(r, k);
			return v ? (int)json_integer_value(v) : def;
		};
		caseR=gi("caseR",18); caseG=gi("caseG",18); caseB=gi("caseB",22);
		railR=gi("railR",30); railG=gi("railG",30); railB=gi("railB",36);
	}
};

// ── Widget ────────────────────────────────────────────────────────────────────

struct PureDreamsWidget : ModuleWidget {
	PDWindow*     pdWin  = nullptr;
	RackBgWidget* rackBg = nullptr;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin  = new PDWindow();
			pdWin->open();
			rackBg = new RackBgWidget(pdWin);
			auto& ch = APP->scene->rack->children;
			auto it = ch.begin();
			if (!ch.empty()) std::advance(it, 1);
			ch.insert(it, rackBg);
			rackBg->parent = APP->scene->rack;
		}

		float cx = box.size.x / 2.f;
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f - 50.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f - 10.f), module, PureDreams::NEXT_PARAM));
		addParam(createParamCentered<Trimpot>(
			Vec(cx, RACK_GRID_HEIGHT/2.f + 40.f), module, PureDreams::BRIGHTNESS_PARAM));
	}

	~PureDreamsWidget() {
		if (rackBg) { APP->scene->rack->removeChild(rackBg); delete rackBg; }
		if (pdWin)  { pdWin->close(); delete pdWin; }
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->requestNext.exchange(false)) pdWin->requestNext = true;
			if (m->requestPrev.exchange(false)) pdWin->requestPrev = true;
			if (rackBg) {
				rackBg->caseColor  = nvgRGB(m->caseR, m->caseG, m->caseB);
				rackBg->railColor  = nvgRGB(m->railR, m->railG, m->railB);
				rackBg->brightness = m->params[PureDreams::BRIGHTNESS_PARAM].getValue();
			}
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(10,10,14)); nvgFill(args.vg);
		nvgSave(args.vg);
		nvgTranslate(args.vg, w/2.f, h/2.f); nvgRotate(args.vg, -M_PI/2.f);
		nvgFontSize(args.vg, 8.f); nvgFillColor(args.vg, nvgRGB(80,80,100));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);
		ModuleWidget::draw(args);
	}

	void appendContextMenu(Menu* menu) override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (!m) return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Case Colour"));

		struct ColorItem : MenuItem {
			PureDreams* m; int r, g, b; bool rail;
			void onAction(const ActionEvent&) override {
				if (rail) { m->railR=r; m->railG=g; m->railB=b; }
				else       { m->caseR=r; m->caseG=g; m->caseB=b; }
			}
		};

		auto addColor = [&](const char* name, int r, int g, int b, bool rail) {
			auto* c = new ColorItem;
			c->text = name; c->m = m; c->r=r; c->g=g; c->b=b; c->rail=rail;
			menu->addChild(c);
		};

		menu->addChild(createMenuLabel("  Rails"));
		addColor("    Default",        30, 30, 36, true);
		addColor("    Charcoal",       55, 55, 65, true);
		addColor("    Midnight blue",  20, 28, 60, true);
		addColor("    Gunmetal",       60, 65, 70, true);
		addColor("    Gold",           80, 65, 20, true);
		addColor("    Bronze",         70, 50, 25, true);

		menu->addChild(createMenuLabel("  Side strips"));
		addColor("    Default",        18, 18, 22, false);
		addColor("    Dark blue",      14, 18, 40, false);
		addColor("    Dark green",     14, 30, 16, false);
		addColor("    Dark red",       36, 14, 14, false);
		addColor("    Dark purple",    28, 14, 40, false);

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Presets (← →)"));

		static std::vector<std::string> presets = getPresetNames();
		int count = 0;
		for (auto& name : presets) {
			if (count++ > 40) { menu->addChild(createMenuLabel("  ...")); break; }
			std::string n = name;
			PDWindow* pw = pdWin;
			int idx = count - 1;
			menu->addChild(createMenuItem(std::string("  ") + n, "", [=]() {
				if (pw) pw->requestPreset = idx;
			}));
		}
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
