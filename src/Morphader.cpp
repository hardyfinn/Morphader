// Morphader: an audio crossfader with fourteen different ways to get from
// A to B. FADE moves across, MODE picks the method, CHARACTER shapes it.
// GLIDE slows FADE down; GO flips to the other side over the glide time.
// The sound engine is in MorphaderDSP.hpp.
#include "plugin.hpp"
#include "MorphaderDSP.hpp"

// ---- LAYOUT (same numbers in res/make_panel.py) ----
static const float DISP_X = 6.0f, DISP_Y = 16.0f, DISP_W = 89.6f, DISP_H = 17.0f;
static const float MODE_X = 17.0f, MODE_Y = 50.0f;
static const float FADE_X = 50.8f, FADE_Y = 51.0f;
static const float CHAR_X = 84.6f, CHAR_Y = 50.0f;
static const float COL[5] = {12.7f, 31.75f, 50.8f, 69.85f, 88.9f};
static const float ROW2_Y = 72.0f;
static const float CV_Y = 89.0f;
static const float IN_Y = 104.0f;
static const float OUT_Y = 117.5f;
static const float OUT_X[2] = {40.8f, 60.8f};

struct Morphader : Module {
	enum ParamIds {
		MODE_PARAM, FADE_PARAM, CHAR_PARAM,
		GLIDE_PARAM, FADE_ATT_PARAM, CHAR_ATT_PARAM,
		GO_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GLIDE_INPUT, FADE_INPUT, MODE_INPUT, CHAR_INPUT, GO_INPUT,
		A_L_INPUT, A_R_INPUT, B_L_INPUT, B_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT, OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		A_LIGHT, B_LIGHT, GO_LIGHT,
		NUM_LIGHTS
	};

	morphader::Engine engine;
	dsp::SchmittTrigger goTrig, goButton;
	bool flipped = false;     // GO swaps which side FADE points at
	float pos = 0.f;          // where the fade actually is (after glide)

	// for the screen
	int dispMode = morphader::POWER;
	float dispChar = 0.5f, dispPos = 0.f, dispGlide = 0.f;

	Morphader() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> names;
		for (int i = 0; i < morphader::NUM_MODES; i++)
			names.push_back(string::f("%d %s", i + 1, morphader::MODE_NAMES[i]));
		configSwitch(MODE_PARAM, 0.f, morphader::NUM_MODES - 1, 0.f, "Mode", names);
		configParam(FADE_PARAM, 0.f, 1.f, 0.f, "Fade (A to B)", "%", 0.f, 100.f);
		configParam(CHAR_PARAM, 0.f, 1.f, 0.5f, "Character (shapes the current mode)", "%", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.f, "Glide (time to travel the whole way)", "", 0.f, 1.f);
		configParam(FADE_ATT_PARAM, -1.f, 1.f, 1.f, "Fade CV amount", "%", 0.f, 100.f);
		configParam(CHAR_ATT_PARAM, -1.f, 1.f, 1.f, "Character CV amount", "%", 0.f, 100.f);
		configButton(GO_PARAM, "GO (flip to the other side over the glide time)");
		configInput(GLIDE_INPUT, "Glide CV (10 V = full)");
		configInput(FADE_INPUT, "Fade CV (0-10 V = A to B, through its amount trim)");
		configInput(MODE_INPUT, "Mode CV (0-10 V steps through all 14 modes)");
		configInput(CHAR_INPUT, "Character CV (10 V = full, through its amount trim)");
		configInput(GO_INPUT, "GO trigger");
		configInput(A_L_INPUT, "A left (or mono)");
		configInput(A_R_INPUT, "A right (normalled to A left)");
		configInput(B_L_INPUT, "B left (or mono)");
		configInput(B_R_INPUT, "B right (normalled to B left)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(A_LIGHT, "A");
		configLight(B_LIGHT, "B");
		configBypass(A_L_INPUT, OUT_L_OUTPUT);
		configBypass(A_R_INPUT, OUT_R_OUTPUT);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}
	void onAdd(const AddEvent& e) override {
		engine.setSampleRate(APP->engine->getSampleRate());
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		flipped = false;
		pos = 0.f;
		engine.resetMode();
	}

	// glide knob -> seconds for a full A-to-B trip (0 = instant)
	static float glideSeconds(float g) {
		if (g <= 0.001f) return 0.f;
		return 0.01f * std::pow(1000.f, g);   // 10 ms .. 10 s
	}

	static float softLimit(float v) {
		float a = std::fabs(v);
		if (a < 8.f) return v;
		float y = 8.f + 3.f * std::tanh((a - 8.f) / 3.f);
		return v < 0.f ? -y : y;
	}

	void process(const ProcessArgs& args) override {
		// GO: button or trigger flips the side
		bool go = goButton.process(params[GO_PARAM].getValue());
		if (goTrig.process(inputs[GO_INPUT].getVoltage(), 0.1f, 1.f)) go = true;
		if (go) flipped = !flipped;

		// mode
		float m = params[MODE_PARAM].getValue() + inputs[MODE_INPUT].getVoltage() * (morphader::NUM_MODES / 10.f);
		int mode = clamp((int) std::floor(m + 0.5f), 0, morphader::NUM_MODES - 1);
		engine.setMode(mode);

		// fade target
		float target = params[FADE_PARAM].getValue()
		               + inputs[FADE_INPUT].getVoltage() * 0.1f * params[FADE_ATT_PARAM].getValue();
		target = clamp(target, 0.f, 1.f);
		if (flipped) target = 1.f - target;

		// glide: constant-speed travel; with no glide, a tiny smoothing only
		float g = clamp(params[GLIDE_PARAM].getValue() + inputs[GLIDE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float secs = glideSeconds(g);
		if (secs <= 0.f) {
			pos += (target - pos) * (1.f - std::exp(-args.sampleTime / 0.002f));
		}
		else {
			float stepMax = args.sampleTime / secs;
			pos += clamp(target - pos, -stepMax, stepMax);
		}

		float c = clamp(params[CHAR_PARAM].getValue()
		                + inputs[CHAR_INPUT].getVoltage() * 0.1f * params[CHAR_ATT_PARAM].getValue(), 0.f, 1.f);

		float aL = inputs[A_L_INPUT].getVoltage();
		float aR = inputs[A_R_INPUT].isConnected() ? inputs[A_R_INPUT].getVoltage() : aL;
		float bL = inputs[B_L_INPUT].getVoltage();
		float bR = inputs[B_R_INPUT].isConnected() ? inputs[B_R_INPUT].getVoltage() : bL;
		float in[2][2] = {{aL, aR}, {bL, bR}};
		float out[2];
		engine.process(in, pos, c, out);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(out[0]));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(out[1]));

		lights[A_LIGHT].setBrightness(1.f - pos);
		lights[B_LIGHT].setBrightness(pos);
		lights[GO_LIGHT].setBrightness(flipped ? 1.f : 0.f);

		dispMode = mode;
		dispChar = c;
		dispPos = pos;
		dispGlide = secs;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "flipped", json_boolean(flipped));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "flipped"))) flipped = json_boolean_value(j);
	}
};

// The screen: mode number and name, what CHARACTER is doing, and a bar
// showing where the fade is between A and B.
struct MorphDisplay : Widget {
	Morphader* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		int mode = morphader::POWER;
		float c = 0.5f, p = 0.35f, glide = 1.2f;
		if (module) {
			mode = module->dispMode; c = module->dispChar; p = module->dispPos; glide = module->dispGlide;
		}
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		float w = box.size.x, h = box.size.y;
		const NVGcolor ink = nvgRGB(238, 232, 220), soft = nvgRGB(169, 163, 154), dim = nvgRGB(110, 104, 98);
		const NVGcolor aCol = nvgRGB(242, 165, 65), bCol = nvgRGB(95, 201, 216);
		float pad = mm2px(2.4f);
		nvgFontFaceId(args.vg, font->handle);

		// mode name
		nvgFontSize(args.vg, mm2px(4.6f));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, ink);
		std::string name = string::f("%02d %s", mode + 1, morphader::MODE_NAMES[mode]);
		nvgText(args.vg, pad, mm2px(6.0f), name.c_str(), NULL);

		// character readout
		char buf[48];
		morphader::Engine::charText(mode, c, buf, sizeof(buf));
		nvgFontSize(args.vg, mm2px(2.9f));
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, soft);
		nvgText(args.vg, w - pad, mm2px(6.0f), buf, NULL);

		// A ---o--- B bar
		float by = mm2px(11.6f);
		nvgFontSize(args.vg, mm2px(3.4f));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, aCol);
		nvgText(args.vg, pad, by, "A", NULL);
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, bCol);
		nvgText(args.vg, w - pad, by, "B", NULL);
		float x0 = mm2px(7.0f), x1 = w - mm2px(7.0f), bh = mm2px(1.8f);
		NVGpaint grad = nvgLinearGradient(args.vg, x0, 0, x1, 0, aCol, bCol);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, x0, by - bh / 2, x1 - x0, bh, bh / 2);
		nvgFillPaint(args.vg, grad);
		nvgGlobalAlpha(args.vg, 0.3f);
		nvgFill(args.vg);
		nvgGlobalAlpha(args.vg, 1.f);
		float px = x0 + (x1 - x0) * clamp(p, 0.f, 1.f);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, x0, by - bh / 2, std::max(px - x0, bh), bh, bh / 2);
		nvgFillPaint(args.vg, grad);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, px, by, mm2px(1.6f));
		nvgFillColor(args.vg, ink);
		nvgFill(args.vg);

		// glide time
		nvgFontSize(args.vg, mm2px(2.5f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, dim);
		std::string gs;
		if (glide <= 0.f) gs = "no glide";
		else if (glide < 1.f) gs = string::f("glide %d ms", (int) (glide * 1000.f + 0.5f));
		else gs = string::f("glide %.1f s", glide);
		nvgText(args.vg, w / 2, h - mm2px(1.2f), gs.c_str(), NULL);

		Widget::drawLayer(args, layer);
	}
};

struct MorphaderWidget : ModuleWidget {
	MorphaderWidget(Morphader* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Morphader.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MorphDisplay* d = createWidget<MorphDisplay>(mm2px(Vec(DISP_X, DISP_Y)));
		d->box.size = mm2px(Vec(DISP_W, DISP_H));
		d->module = module;
		addChild(d);

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(MODE_X, MODE_Y)), module, Morphader::MODE_PARAM));
		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(FADE_X, FADE_Y)), module, Morphader::FADE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(CHAR_X, CHAR_Y)), module, Morphader::CHAR_PARAM));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[0], ROW2_Y)), module, Morphader::GLIDE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(COL[1], ROW2_Y)), module, Morphader::FADE_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(COL[3], ROW2_Y)), module, Morphader::CHAR_ATT_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(COL[4], ROW2_Y)), module, Morphader::GO_PARAM, Morphader::GO_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(COL[2] - 4.f, ROW2_Y)), module, Morphader::A_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(COL[2] + 4.f, ROW2_Y)), module, Morphader::B_LIGHT));

		const int cv[5] = {Morphader::GLIDE_INPUT, Morphader::FADE_INPUT, Morphader::MODE_INPUT, Morphader::CHAR_INPUT, Morphader::GO_INPUT};
		for (int i = 0; i < 5; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[i], CV_Y)), module, cv[i]));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[0], IN_Y)), module, Morphader::A_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[1], IN_Y)), module, Morphader::A_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[3], IN_Y)), module, Morphader::B_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[4], IN_Y)), module, Morphader::B_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[0], OUT_Y)), module, Morphader::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[1], OUT_Y)), module, Morphader::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Morphader* module = dynamic_cast<Morphader*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("GO flipped (FADE points the other way)", "", &module->flipped));
	}
};

Model* modelMorphader = createModel<Morphader, MorphaderWidget>("Morphader");
