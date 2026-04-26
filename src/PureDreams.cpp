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
			x1-=pad; y1-=pad+2; x2+=pad; y2+=pad+2;
			float fw = x2-x1, fh = y2-y1;
			float rr  = railColor.r, rg = railColor.g, rb = railColor.b;

			// ── Soft drop shadow (always visible) ─────────────────────────
			for (int i = 5; i >= 1; i--) {
				float off = i * 5.f;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x1+off, y1+off, fw, fh, 8.f);
				nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 25));
				nvgFill(args.vg);
			}

			// ── Case inner background (solid, brightness-controlled) ───────
			float bgAlpha = brightness; // 1 = solid, 0 = transparent (show projectM)
			NVGpaint bgGrad = nvgLinearGradient(args.vg,
				x1, y1 + railH,
				x1, y2 - railH,
				nvgRGBAf(caseColor.r + 0.06f, caseColor.g + 0.06f, caseColor.b + 0.06f, bgAlpha),
				nvgRGBAf(caseColor.r - 0.02f, caseColor.g - 0.02f, caseColor.b - 0.02f, bgAlpha));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x1 + 8.f, y1 + railH, fw - 16.f, fh - railH * 2);
			nvgFillPaint(args.vg, bgGrad);
			nvgFill(args.vg);

			// ── Top rail — metallic gradient ──────────────────────────────
			NVGpaint topRail = nvgLinearGradient(args.vg,
				x1, y1, x1, y1 + railH,
				nvgRGBAf(rr+0.15f, rg+0.15f, rb+0.15f, 0.98f),  // bright highlight
				nvgRGBAf(rr-0.04f, rg-0.04f, rb-0.04f, 0.98f)); // shadow
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, railH, 6.f);
			nvgFillPaint(args.vg, topRail);
			nvgFill(args.vg);

			// ── Bottom rail ───────────────────────────────────────────────
			NVGpaint botRail = nvgLinearGradient(args.vg,
				x1, y2 - railH, x1, y2,
				nvgRGBAf(rr+0.10f, rg+0.10f, rb+0.10f, 0.98f),
				nvgRGBAf(rr-0.06f, rg-0.06f, rb-0.06f, 0.98f));
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y2 - railH, fw, railH, 6.f);
			nvgFillPaint(args.vg, botRail);
			nvgFill(args.vg);

			// ── Rail highlight line (top edge) ────────────────────────────
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x1 + 8, y1 + 1); nvgLineTo(args.vg, x2 - 8, y1 + 1);
			nvgStrokeColor(args.vg, nvgRGBAf(1,1,1, 0.25f));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			// ── Screw holes (top rail) ────────────────────────────────────
			auto drawScrew = [&](float sx, float sy) {
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, sx, sy, 4.5f);
				NVGpaint sp = nvgRadialGradient(args.vg, sx-1, sy-1, 0.5f, 4.5f,
					nvgRGB(70,70,78), nvgRGB(28,28,34));
				nvgFillPaint(args.vg, sp);
				nvgFill(args.vg);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, sx, sy, 4.5f);
				nvgStrokeColor(args.vg, nvgRGB(20,20,24));
				nvgStrokeWidth(args.vg, 0.8f);
				nvgStroke(args.vg);
				// Slot
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, sx-2.5f, sy); nvgLineTo(args.vg, sx+2.5f, sy);
				nvgStrokeColor(args.vg, nvgRGBA(0,0,0,120));
				nvgStrokeWidth(args.vg, 1.2f);
				nvgStroke(args.vg);
			};
			float screwY1 = y1 + railH * 0.5f;
			float screwY2 = y2 - railH * 0.5f;
			float screwOff = 10.f;
			drawScrew(x1 + screwOff, screwY1); drawScrew(x2 - screwOff, screwY1);
			drawScrew(x1 + screwOff, screwY2); drawScrew(x2 - screwOff, screwY2);
			// Additional screws along rails every 120px
			for (float sx = x1 + 120.f; sx < x2 - 60.f; sx += 120.f) {
				drawScrew(sx, screwY1);
				drawScrew(sx, screwY2);
			}

			// ── Left/right side panels ────────────────────────────────────
			float cr = caseColor.r, cg = caseColor.g, cb = caseColor.b;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x1, y1 + railH, 8.f, fh - railH * 2);
			nvgFillColor(args.vg, nvgRGBAf(cr, cg, cb, 0.97f));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x2 - 8.f, y1 + railH, 8.f, fh - railH * 2);
			nvgFillColor(args.vg, nvgRGBAf(cr, cg, cb, 0.97f));
			nvgFill(args.vg);

			// ── Outer border with 3D bevel ────────────────────────────────
			// Dark outer edge
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, fh, 8.f);
			nvgStrokeColor(args.vg, nvgRGB(15,15,18));
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
			// Inner highlight (top-left) for 3D depth
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1+1.5f, y1+1.5f, fw-3.f, fh-3.f, 7.f);
			nvgStrokeColor(args.vg, nvgRGBAf(1,1,1, 0.08f));
			nvgStrokeWidth(args.vg, 1.f);
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

	int caseR=110, caseG=110, caseB=114;  // default: medium grey
	int railR= 55, railG= 55, railB= 62;  // default: gunmetal

	PureDreams() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(PREV_PARAM, "Prev preset");
		configButton(NEXT_PARAM, "Next preset");
		configParam(BRIGHTNESS_PARAM, 0.f, 1.f, 0.92f, "Case opacity");
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
		addColor("    Gunmetal (default)", 55, 55, 62, true);
		addColor("    Charcoal",           70, 70, 78, true);
		addColor("    Midnight blue",      35, 45, 80, true);
		addColor("    Black",              28, 28, 32, true);
		addColor("    Gold",              100, 80, 25, true);
		addColor("    Bronze",             90, 65, 35, true);

		menu->addChild(createMenuLabel("  Case background"));
		addColor("    Grey (default)",    110,110,114, false);
		addColor("    Dark grey",          70, 70, 75, false);
		addColor("    Midnight blue",      55, 65,110, false);
		addColor("    Forest green",       55, 90, 65, false);
		addColor("    Dark red",          110, 55, 55, false);
		addColor("    Deep purple",        75, 55,115, false);
		addColor("    Black",              28, 28, 32, false);

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
