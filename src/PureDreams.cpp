#include "plugin.hpp"
#include "PDWindow.hpp"
#include <mutex>
#include <list>
#include <algorithm>

// Draws projectM pixels as background, injected directly into RackWidget
// before ModuleContainer so it renders behind all modules.
struct RackBgWidget : widget::Widget {
	PDWindow* pdWin;
	int nvgImg = -1;

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

		if (nvgImg >= 0) {
			// Use clipBox — the visible portion of the rack in rack coordinates
			float cx = args.clipBox.pos.x, cy = args.clipBox.pos.y;
			float cw = args.clipBox.size.x, ch = args.clipBox.size.y;
			if (cw <= 0 || ch <= 0) { cw = w; ch = h; cx = 0; cy = 0; }

			float imgW = pdWin->pixelW, imgH = pdWin->pixelH;
			float scale = std::max(cw / imgW, ch / imgH);
			float pw = imgW * scale, ph = imgH * scale;
			float ox = cx + (cw - pw) / 2.f;

			// Y-flip: translate to bottom of clip area, flip, draw
			nvgSave(args.vg);
			nvgTranslate(args.vg, 0, cy + ch);
			nvgScale(args.vg, 1.f, -1.f);
			NVGpaint p = nvgImagePattern(args.vg, ox, (ch - ph) / 2.f, pw, ph, 0.f, nvgImg, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, cx, 0, cw, ch);
			nvgFillPaint(args.vg, p);
			nvgFill(args.vg);
			nvgRestore(args.vg);
		} else {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, w, h);
			nvgFillColor(args.vg, nvgRGB(14, 14, 18));
			nvgFill(args.vg);
		}

		// Eurorack case frame around all modules
		auto mws = APP->scene->rack->getModuleWidgets();
		if (!mws.empty()) {
			float pad = 16.f;
			float x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
			for (auto* mw : mws) {
				x1 = std::min(x1, mw->box.pos.x);
				y1 = std::min(y1, mw->box.pos.y);
				x2 = std::max(x2, mw->box.pos.x + mw->box.size.x);
				y2 = std::max(y2, mw->box.pos.y + mw->box.size.y);
			}
			x1 -= pad; y1 -= pad; x2 += pad; y2 += pad;
			float fw = x2 - x1, fh = y2 - y1;

			// Case shadow
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1 + 4, y1 + 4, fw, fh, 6.f);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 120));
			nvgFill(args.vg);

			// Case body (dark semi-transparent)
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, fh, 6.f);
			nvgFillColor(args.vg, nvgRGBA(12, 12, 16, 210));
			nvgFill(args.vg);

			// Case border
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, fh, 6.f);
			nvgStrokeColor(args.vg, nvgRGB(55, 55, 65));
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);

			// Top rail strip
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x1, y1, fw, pad, 6.f);
			nvgFillColor(args.vg, nvgRGB(22, 22, 28));
			nvgFill(args.vg);

			// Bottom rail strip
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x1, y2 - pad, fw, pad);
			nvgFillColor(args.vg, nvgRGB(22, 22, 28));
			nvgFill(args.vg);
		}
	}
};

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
	RackBgWidget* rackBg = nullptr;

	PureDreamsWidget(PureDreams* module) {
		setModule(module);
		box.size = Vec(2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		if (module) {
			pdWin = new PDWindow();
			pdWin->open();

			rackBg = new RackBgWidget(pdWin);

			// Insert at position 1: after RailWidget, before ModuleContainer
			// This makes it draw before all modules.
			auto& children = APP->scene->rack->children;
			auto it = children.begin();
			if (!children.empty()) std::advance(it, 1);
			children.insert(it, rackBg);
			rackBg->parent = APP->scene->rack;
		}

		float cx = box.size.x / 2.f;
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f - 30.f), module, PureDreams::PREV_PARAM));
		addParam(createParamCentered<VCVBezel>(
			Vec(cx, RACK_GRID_HEIGHT/2.f + 30.f), module, PureDreams::NEXT_PARAM));
	}

	~PureDreamsWidget() {
		if (rackBg) {
			APP->scene->rack->removeChild(rackBg);
			delete rackBg;
		}
		if (pdWin) { pdWin->close(); delete pdWin; }
	}

	void step() override {
		PureDreams* m = dynamic_cast<PureDreams*>(this->module);
		if (m && pdWin) {
			if (m->params[PureDreams::NEXT_PARAM].getValue() > 0.f) pdWin->requestNext = true;
			if (m->params[PureDreams::PREV_PARAM].getValue() > 0.f) pdWin->requestPrev = true;
		}
		ModuleWidget::step();
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(args.vg); nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(10, 10, 14)); nvgFill(args.vg);
		nvgSave(args.vg);
		nvgTranslate(args.vg, w/2.f, h/2.f); nvgRotate(args.vg, -M_PI/2.f);
		nvgFontSize(args.vg, 8.f); nvgFillColor(args.vg, nvgRGB(80, 80, 100));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
		nvgText(args.vg, 0, 0, "PURE DREAMS", nullptr);
		nvgRestore(args.vg);
		ModuleWidget::draw(args);
	}
};

Model* modelPureDreams = createModel<PureDreams, PureDreamsWidget>("PureDreams");
