// Morphader sound engine: fourteen different ways to get from source A to
// source B. No Rack code in here, so test/render.cpp can run it offline.
//
// Every mode follows the same contract:
//   FADE = 0   -> the output is exactly A
//   FADE = 1   -> the output is exactly B (once any tails have died away)
//   in between -> the mode's own kind of transition
// CHARACTER (0..1) shapes each mode (rate, resonance, size and so on).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace morphader {

static const int NUM_MODES = 14;
enum Mode {
	FILTER, CHOP, RING, VOCODER, SPECTRAL, TAPE, CRUSH,
	DUB, WASH, GRAIN, FREEZE, ENVSWAP, FOLD, POWER
};
static const char* const MODE_NAMES[NUM_MODES] = {
	"FILTER SWAP", "GATE CHOP", "RING", "VOCODER", "SPECTRAL", "TAPE STOP", "CRUSH",
	"DUB THROW", "REVERB WASH", "GRAIN SHUFFLE", "FREEZE", "ENV SWAP", "FOLD", "EQUAL POWER"
};

static const float PI = 3.14159265358979f;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
// equal-power pair
inline float epA(float x) { return std::cos(clamp01(x) * PI * 0.5f); }
inline float epB(float x) { return std::sin(clamp01(x) * PI * 0.5f); }
// "wide" equal-power: k = 1 is plain equal power, bigger k keeps both sides
// up for longer (k = 8 is a DJ-style cut where both are full in the middle)
inline void wide(float x, float k, float& ga, float& gb) {
	ga = std::sin(clamp01((1.f - x) * k) * PI * 0.5f);
	gb = std::sin(clamp01(x * k) * PI * 0.5f);
}
// equal-power blend of two signals, t = 0 -> a, t = 1 -> b
inline float xfade(float a, float b, float t) { return a * epA(t) + b * epB(t); }

// Zero-delay state-variable filter (Cytomic / Simper)
struct SVF {
	float ic1 = 0.f, ic2 = 0.f;
	void reset() { ic1 = ic2 = 0.f; }
	// g = tan(pi fc / sr), k = 1 / Q
	inline void process(float v0, float g, float k, float& lp, float& bp, float& hp) {
		float a1 = 1.f / (1.f + g * (g + k));
		float a2 = g * a1;
		float a3 = g * a2;
		float v3 = v0 - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		lp = v2; bp = v1; hp = v0 - k * v1 - v2;
	}
	inline float lowpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return l; }
	inline float bandpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return b; }
	inline float highpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return h; }
};
inline float svfG(float fc, float sr) {
	fc = std::min(std::max(fc, 5.f), sr * 0.45f);
	return std::tan(PI * fc / sr);
}

// Circular buffer; read(d) gives the sample d samples ago (d = 1 is the newest)
struct Ring {
	std::vector<float> b;
	int n = 1, w = 0;
	void init(int size) { n = std::max(size, 4); b.assign(n, 0.f); w = 0; }
	void clear() { std::fill(b.begin(), b.end(), 0.f); }
	inline void push(float v) { b[w] = v; if (++w >= n) w = 0; }
	inline float read(float d) const {
		d = std::min(std::max(d, 1.f), (float) (n - 2));
		float rp = (float) w - d;
		while (rp < 0.f) rp += (float) n;
		int i0 = (int) rp;
		float f = rp - (float) i0;
		int i1 = i0 + 1;
		if (i0 >= n) i0 -= n;
		if (i1 >= n) i1 -= n;
		return b[i0] + (b[i1] - b[i0]) * f;
	}
	inline float readInt(int d) const {
		int i = w - d;
		while (i < 0) i += n;
		return b[i];
	}
};

struct Follower {
	float e = 0.f;
	inline float process(float v, float att, float rel) {
		float a = std::fabs(v);
		e += (a - e) * (a > e ? att : rel);
		return e;
	}
};
inline float coefFor(float seconds, float sr) {
	return 1.f - std::exp(-1.f / std::max(seconds * sr, 1.f));
}

static const int VOC_BANDS = 16;
static const int SPEC_SPLITS = 7;   // 8 bands
static const float SPEC_FREQS[SPEC_SPLITS] = {120.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f};
static const int REV_LINES = 8;
static const float REV_MS[REV_LINES] = {29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.9f, 67.3f, 73.1f};
static const float REV_AP_MS[4] = {4.7f, 3.6f, 12.7f, 9.3f};

// state for one stereo side (L or R)
struct Chan {
	// filter swap
	SVF fA, fB;
	// vocoder
	SVF vA[VOC_BANDS], vB[VOC_BANDS];
	Follower veA[VOC_BANDS], veB[VOC_BANDS];
	// spectral
	SVF sA[SPEC_SPLITS], sB[SPEC_SPLITS];
	// 2-second history of both sources (tape stop, grain shuffle)
	Ring hA, hB;
	// tape stop
	float tdA = 0.f, tdB = 0.f;
	float tlpA = 0.f, tlpB = 0.f;
	// crush
	float holdA = 0.f, holdB = 0.f, holdPhA = 1.f, holdPhB = 1.f;
	// dub throw
	Ring dub;
	float dubLP = 0.f, dubHPs = 0.f, dubT = 0.f;
	// freeze
	Ring frz;
	// envelope swap
	Follower envA, envB;
	// vocoder level matching
	Follower lvAB, lvBA, lvA, lvB;
	// mode switch dc / misc
	void reset() {
		fA.reset(); fB.reset();
		for (int i = 0; i < VOC_BANDS; i++) { vA[i].reset(); vB[i].reset(); veA[i].e = veB[i].e = 0.f; }
		for (int i = 0; i < SPEC_SPLITS; i++) { sA[i].reset(); sB[i].reset(); }
		tdA = tdB = 0.f; tlpA = tlpB = 0.f;
		holdA = holdB = 0.f; holdPhA = holdPhB = 1.f;
		dub.clear(); dubLP = dubHPs = 0.f;
		envA.e = envB.e = 0.f;
		lvAB.e = lvBA.e = lvA.e = lvB.e = 0.f;
	}
};

struct Engine {
	float sr = 48000.f;
	int mode = POWER;          // the mode that is sounding
	int modeReq = POWER;       // the mode asked for
	float switchGain = 1.f;    // dips to 0 for a few ms when the mode changes
	float cs = 0.5f;           // smoothed character
	uint32_t rng = 0x9E3779B9u;
	Chan ch[2];

	// precomputed filter coefficients
	float vocG[VOC_BANDS] = {};
	float specG[SPEC_SPLITS] = {};

	// gate chop
	float chopPh = 0.f, chopS = 0.f;
	// reverb wash
	Ring ap[2][4];
	Ring line[REV_LINES];
	int lineLen[REV_LINES] = {};
	int apLen[4] = {};
	float damp[REV_LINES] = {};
	// grain shuffle
	int gPos = 1 << 30, gLen = 1000, gXf = 64;
	int curSrc = 0, prevSrc = 0;
	float curOff = 1.f, prevOff = 1.f;
	// freeze
	bool frozen = false;
	float lf = 0.f;     // 0 = live, 1 = frozen loop
	float frzP = 0.f;   // loop play position (samples)

	// what the screen shows
	float lastX = 0.f;

	Engine() { setSampleRate(48000.f); }

	inline float rnd() {
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return (rng >> 8) * (1.f / 16777216.f);
	}

	void setSampleRate(float s) {
		sr = s;
		for (int c = 0; c < 2; c++) {
			ch[c].hA.init((int) (2.2f * sr));
			ch[c].hB.init((int) (2.2f * sr));
			ch[c].dub.init((int) (2.2f * sr));
			ch[c].frz.init((int) (2.2f * sr));
			for (int i = 0; i < 4; i++) {
				apLen[i] = std::max(2, (int) (REV_AP_MS[i] * 0.001f * sr * (c ? 1.07f : 1.f)));
				ap[c][i].init(apLen[i] + 4);
			}
			ch[c].reset();
		}
		for (int i = 0; i < 4; i++) apLen[i] = std::max(2, (int) (REV_AP_MS[i] * 0.001f * sr));
		for (int i = 0; i < REV_LINES; i++) {
			lineLen[i] = std::max(4, (int) (REV_MS[i] * 0.001f * sr));
			line[i].init(lineLen[i] + 4);
			damp[i] = 0.f;
		}
		for (int i = 0; i < VOC_BANDS; i++) {
			float f = 100.f * std::pow(80.f, i / (float) (VOC_BANDS - 1));   // 100 Hz .. 8 kHz
			vocG[i] = svfG(f, sr);
		}
		for (int i = 0; i < SPEC_SPLITS; i++) specG[i] = svfG(SPEC_FREQS[i], sr);
		frozen = false; lf = 0.f;
	}

	void resetMode() {
		for (int c = 0; c < 2; c++) ch[c].reset();
		for (int i = 0; i < REV_LINES; i++) { line[i].clear(); damp[i] = 0.f; }
		for (int c = 0; c < 2; c++) for (int i = 0; i < 4; i++) ap[c][i].clear();
		chopPh = 0.f; chopS = 0.f;
		gPos = 1 << 30;
		frozen = false; lf = 0.f; frzP = 0.f;
	}

	void setMode(int m) { modeReq = std::min(std::max(m, 0), NUM_MODES - 1); }

	// --- CHARACTER mappings (also used for the screen) ---
	static float chopRate(float c) { return 0.5f * std::pow(4000.f, c); }       // 0.5 Hz .. 2 kHz
	static float ringWidth(float c) { return 0.08f + 0.42f * c; }
	static float vocRelease(float c) { return 0.3f * std::pow(5.f / 300.f, c); } // 300 ms .. 5 ms
	static float specWidth(float c) { return 0.02f + 0.3f * c; }
	static float tapeDepth(float c) { return 1.f + 3.f * c; }                   // octaves
	static float dubTime(float c) { return 0.06f * std::pow(1.f / 0.06f, c); }   // 60 ms .. 1 s
	static float washDecay(float c) { return 0.6f * std::pow(20.f, c); }        // RT60 0.6 .. 12 s
	static float grainLen(float c) { return 0.005f * std::pow(80.f, c); }       // 5 .. 400 ms
	static float loopLen(float c) { return 0.03f * std::pow(1.f / 0.03f, c); }   // 30 ms .. 1 s
	static float envRelease(float c) { return 0.005f * std::pow(60.f, c); }     // 5 .. 300 ms
	static float foldDrive(float c) { return 2.f + 10.f * c; }

	// Short text for the screen, describing what CHARACTER is doing
	static void charText(int m, float c, char* buf, int n) {
		switch (m) {
			case FILTER: snprintf(buf, n, "resonance %d%%", (int) (c * 100.f + 0.5f)); break;
			case CHOP: {
				float f = chopRate(c);
				if (f < 10.f) snprintf(buf, n, "rate %.2f Hz", f);
				else snprintf(buf, n, "rate %d Hz", (int) (f + 0.5f));
			} break;
			case RING: snprintf(buf, n, "ring zone %d%%", (int) (ringWidth(c) * 200.f + 0.5f)); break;
			case VOCODER: snprintf(buf, n, "release %d ms", (int) (vocRelease(c) * 1000.f + 0.5f)); break;
			case SPECTRAL: snprintf(buf, n, "overlap %d%%", (int) (c * 100.f + 0.5f)); break;
			case TAPE: snprintf(buf, n, "drop %.1f oct", tapeDepth(c)); break;
			case CRUSH:
				if (c < 0.35f) snprintf(buf, n, "bits");
				else if (c > 0.65f) snprintf(buf, n, "sample rate");
				else snprintf(buf, n, "bits + rate");
				break;
			case DUB: snprintf(buf, n, "echo %d ms", (int) (dubTime(c) * 1000.f + 0.5f)); break;
			case WASH: snprintf(buf, n, "decay %.1f s", washDecay(c)); break;
			case GRAIN: snprintf(buf, n, "grain %d ms", (int) (grainLen(c) * 1000.f + 0.5f)); break;
			case FREEZE: snprintf(buf, n, "loop %d ms", (int) (loopLen(c) * 1000.f + 0.5f)); break;
			case ENVSWAP: snprintf(buf, n, "release %d ms", (int) (envRelease(c) * 1000.f + 0.5f)); break;
			case FOLD: snprintf(buf, n, "fold x%.1f", foldDrive(c)); break;
			case POWER:
				if (c < 0.2f) snprintf(buf, n, "linear");
				else if (c < 0.6f) snprintf(buf, n, "equal power");
				else if (c < 0.85f) snprintf(buf, n, "wide");
				else snprintf(buf, n, "cut");
				break;
		}
	}

	// in[src][side]: src 0 = A, 1 = B; side 0 = L, 1 = R. x = fade 0..1, c = character 0..1
	void process(const float in[2][2], float x, float c, float out[2]) {
		x = clamp01(x);
		cs += (clamp01(c) - cs) * 0.002f;
		c = cs;
		lastX = x;

		// mode change: dip out, swap, dip in (about 4 ms each way)
		float step = 1.f / (0.004f * sr);
		if (modeReq != mode) {
			switchGain -= step;
			if (switchGain <= 0.f) {
				switchGain = 0.f;
				mode = modeReq;
				resetMode();
			}
		}
		else if (switchGain < 1.f) {
			switchGain = std::min(1.f, switchGain + step);
		}

		// history buffers are always written
		for (int s = 0; s < 2; s++) {
			ch[s].hA.push(in[0][s]);
			ch[s].hB.push(in[1][s]);
		}

		switch (mode) {
			case FILTER: modeFilter(in, x, c, out); break;
			case CHOP: modeChop(in, x, c, out); break;
			case RING: modeRing(in, x, c, out); break;
			case VOCODER: modeVocoder(in, x, c, out); break;
			case SPECTRAL: modeSpectral(in, x, c, out); break;
			case TAPE: modeTape(in, x, c, out); break;
			case CRUSH: modeCrush(in, x, c, out); break;
			case DUB: modeDub(in, x, c, out); break;
			case WASH: modeWash(in, x, c, out); break;
			case GRAIN: modeGrain(in, x, c, out); break;
			case FREEZE: modeFreeze(in, x, c, out); break;
			case ENVSWAP: modeEnvSwap(in, x, c, out); break;
			case FOLD: modeFold(in, x, c, out); break;
			default: modePower(in, x, c, out); break;
		}
		for (int s = 0; s < 2; s++) {
			float v = out[s] * switchGain;
			if (!std::isfinite(v)) { v = 0.f; resetMode(); }
			out[s] = v;
		}
	}

	// 1. A's low-pass closes while B's high-pass opens; they meet in the middle
	void modeFilter(const float in[2][2], float x, float c, float out[2]) {
		float k = 2.f - 1.85f * c;     // resonance
		float gA = svfG(20000.f * std::exp(std::log(0.002f) * x), sr);    // 20 kHz -> 40 Hz
		float gB = svfG(20000.f * std::exp(std::log(0.001f) * x), sr);    // 20 kHz -> 20 Hz
		float wetA = clamp01(x * 20.f), wetB = clamp01((1.f - x) * 20.f);
		float lvA = clamp01((1.f - x) * 8.f), lvB = clamp01(x * 8.f);
		for (int s = 0; s < 2; s++) {
			float a = ch[s].fA.lowpass(in[0][s], gA, k);
			float b = ch[s].fB.highpass(in[1][s], gB, k);
			a = in[0][s] + (a - in[0][s]) * wetA;
			b = in[1][s] + (b - in[1][s]) * wetB;
			out[s] = a * lvA + b * lvB;
		}
	}

	// 2. rapid switching; the share of time on B follows FADE (50/50 in the centre)
	void modeChop(const float in[2][2], float x, float c, float out[2]) {
		float f = chopRate(c);
		chopPh += f / sr;
		if (chopPh >= 1.f) chopPh -= std::floor(chopPh);
		float target = (x >= 1.f || chopPh < x) ? 1.f : 0.f;
		if (x <= 0.f) target = 0.f;
		float tau = std::min(0.001f, 0.08f / f);
		chopS += (target - chopS) * coefFor(tau, sr);
		for (int s = 0; s < 2; s++)
			out[s] = in[0][s] * (1.f - chopS) + in[1][s] * chopS;
	}

	// 3. pure A -> A x B ring modulation at the centre -> pure B
	void modeRing(const float in[2][2], float x, float c, float out[2]) {
		float w = ringWidth(c);
		float r = clamp01(1.f - std::fabs(x - 0.5f) / w);
		r = r * r * (3.f - 2.f * r);
		float ga = epA(x) * (1.f - r), gb = epB(x) * (1.f - r);
		for (int s = 0; s < 2; s++) {
			float ring = 7.f * std::tanh(in[0][s] * in[1][s] * 0.06f);
			out[s] = in[0][s] * ga + in[1][s] * gb + ring * r;
		}
	}

	// 4. A -> A speaking with B's spectrum -> B speaking with A's -> B
	void modeVocoder(const float in[2][2], float x, float c, float out[2]) {
		const float k = 0.3f;
		float rel = vocRelease(c);
		float att = coefFor(std::min(0.002f, rel), sr);
		float rc = coefFor(rel, sr);
		float agc = coefFor(0.08f, sr);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float vAB = 0.f, vBA = 0.f;
			for (int i = 0; i < VOC_BANDS; i++) {
				float ba = h.vA[i].bandpass(in[0][s], vocG[i], k);
				float bb = h.vB[i].bandpass(in[1][s], vocG[i], k);
				float ea = h.veA[i].process(ba, att, rc);
				float eb = h.veB[i].process(bb, att, rc);
				vAB += ba * eb / (ea + 0.5f);
				vBA += bb * ea / (eb + 0.5f);
			}
			// level matching: A-with-B's-spectrum sits at B's loudness and vice versa
			float la = h.lvA.process(in[0][s], agc, agc), lb = h.lvB.process(in[1][s], agc, agc);
			vAB *= 0.25f; vBA *= 0.25f;
			float lab = h.lvAB.process(vAB, agc, agc), lba = h.lvBA.process(vBA, agc, agc);
			vAB *= std::min(std::max(lb / (lab + 0.1f), 0.25f), 2.f);
			vBA *= std::min(std::max(la / (lba + 0.1f), 0.25f), 2.f);
			vAB = 8.f * std::tanh(vAB * 0.125f);
			vBA = 8.f * std::tanh(vBA * 0.125f);
			float pts[4] = {in[0][s], vAB, vBA, in[1][s]};
			float seg = x * 3.f;
			int i = std::min((int) seg, 2);
			out[s] = xfade(pts[i], pts[i + 1], seg - (float) i);
		}
	}

	// 5. eight bands; B takes them over one by one from the bottom up
	void modeSpectral(const float in[2][2], float x, float c, float out[2]) {
		const float k = 1.41421f;
		float w = specWidth(c);
		float tg[SPEC_SPLITS + 1];
		for (int b = 0; b <= SPEC_SPLITS; b++) {
			float p = w * 0.5f + (1.f - w) * (b + 0.5f) / (SPEC_SPLITS + 1);
			tg[b] = clamp01((x - p) / w + 0.5f);
		}
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float prevA = 0.f, prevB = 0.f, acc = 0.f;
			for (int b = 0; b < SPEC_SPLITS; b++) {
				float la = h.sA[b].lowpass(in[0][s], specG[b], k);
				float lb = h.sB[b].lowpass(in[1][s], specG[b], k);
				acc += xfade(la - prevA, lb - prevB, tg[b]);
				prevA = la; prevB = lb;
			}
			acc += xfade(in[0][s] - prevA, in[1][s] - prevB, tg[SPEC_SPLITS]);
			// exact at the ends
			if (x <= 0.f) acc = in[0][s];
			if (x >= 1.f) acc = in[1][s];
			out[s] = acc;
		}
	}

	// pitch shifter used by tape stop: two taps sliding through a window
	inline float shift(const Ring& r, float& td, float rate, float W) {
		td += 1.f - rate;
		if (td >= W) td -= W;
		if (td < 0.f) td += W;
		float t2 = td + W * 0.5f;
		if (t2 >= W) t2 -= W;
		float g1 = std::sin(PI * td / W), g2 = std::sin(PI * t2 / W);
		return r.read(2.f + td) * g1 + r.read(2.f + t2) * g2;
	}

	// 6. A slows and drops in pitch while B spins up from low
	void modeTape(const float in[2][2], float x, float c, float out[2]) {
		float D = tapeDepth(c);
		float pa = -D * std::pow(x, 1.5f), pb = -D * std::pow(1.f - x, 1.5f);
		float ra = std::exp2(pa), rb = std::exp2(pb);
		float amtA = clamp01(-pa * 24.f), amtB = clamp01(-pb * 24.f);
		float W = 0.08f * sr;
		float lpa = 1.f - std::exp(-2.f * PI * std::min(18000.f * ra, sr * 0.45f) / sr);
		float lpb = 1.f - std::exp(-2.f * PI * std::min(18000.f * rb, sr * 0.45f) / sr);
		float ga, gb;
		wide(x, 1.3f, ga, gb);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float a = in[0][s], b = in[1][s];
			if (amtA > 0.f) {
				float sa = shift(h.hA, h.tdA, ra, W);
				h.tlpA += (sa - h.tlpA) * lpa;
				a = a + (h.tlpA - a) * amtA;
			}
			else { h.tdA = 0.f; h.tlpA = a; }
			if (amtB > 0.f) {
				float sb = shift(h.hB, h.tdB, rb, W);
				h.tlpB += (sb - h.tlpB) * lpb;
				b = b + (h.tlpB - b) * amtB;
			}
			else { h.tdB = 0.f; h.tlpB = b; }
			out[s] = a * ga + b * gb;
		}
	}

	static inline float crushOne(float v, float sev, float bitAmt, float rateAmt, float& hold, float& ph) {
		float bits = 16.f - 14.5f * sev * bitAmt;
		float stepQ = 10.f / std::exp2(bits);
		float hf = 1.f + sev * rateAmt * 63.f;
		ph += 1.f;
		if (ph >= hf) {
			ph -= hf;
			hold = v;
		}
		if (hf <= 1.001f) hold = v;
		return std::round(hold / stepQ) * stepQ;
	}

	// 7. A crushes into grit while B resolves out of it
	void modeCrush(const float in[2][2], float x, float c, float out[2]) {
		float bitAmt = std::min(1.f, 2.f * (1.f - c)), rateAmt = std::min(1.f, 2.f * c);
		float ga, gb;
		wide(x, 1.5f, ga, gb);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float a = crushOne(in[0][s], x, bitAmt, rateAmt, h.holdA, h.holdPhA);
			float b = crushOne(in[1][s], 1.f - x, bitAmt, rateAmt, h.holdB, h.holdPhB);
			if (x <= 0.f) a = in[0][s];
			if (x >= 1.f) b = in[1][s];
			out[s] = a * ga + b * gb;
		}
	}

	// 8. A is thrown into a dub delay; its echoes trail off as B arrives
	void modeDub(const float in[2][2], float x, float c, float out[2]) {
		float send = x < 0.6f ? epB(x / 0.6f) : (x < 0.9f ? epA((x - 0.6f) / 0.3f) : 0.f);
		float fb = 0.35f + 0.5f * x;
		float gA = epA(x * 1.4f);
		float gB = epB((x - 0.3f) / 0.7f);
		float lp = coefFor(1.f / (2.f * PI * 2500.f), sr);
		float hp = coefFor(1.f / (2.f * PI * 100.f), sr);
		float tSmooth = coefFor(0.05f, sr);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float T = dubTime(c) * (s ? 0.75f : 1.f) * sr;
			if (h.dubT <= 0.f) h.dubT = T;
			h.dubT += (T - h.dubT) * tSmooth;
			float wet = h.dub.read(h.dubT);
			// feedback path: darker, thinner, a little saturated
			h.dubLP += (wet - h.dubLP) * lp;
			h.dubHPs += (h.dubLP - h.dubHPs) * hp;
			float fbv = std::tanh((h.dubLP - h.dubHPs) * fb * 0.2f) * 5.f;
			h.dub.push(in[0][s] * send + fbv);
			out[s] = in[0][s] * gA + in[1][s] * gB + wet * 0.8f;
		}
	}

	// 9. A drowns in reverb; B emerges dry out of the wash
	void modeWash(const float in[2][2], float x, float c, float out[2]) {
		auto sendOf = [](float v) { return v < 0.6f ? epB(v / 0.6f) : epA((v - 0.6f) / 0.4f); };
		float sA = sendOf(x), sB = sendOf(1.f - x);
		float dA = epA(x * 1.5f), dB = epB((x - 0.33f) * 1.5f);
		float rt = washDecay(c);
		float dampC = coefFor(1.f / (2.f * PI * 6000.f), sr);
		float inS[2];
		for (int s = 0; s < 2; s++) {
			float v = in[0][s] * sA + in[1][s] * sB;
			// input diffusion
			for (int i = 0; i < 4; i++) {
				float d = ap[s][i].readInt(apLen[i]);
				float y = -0.6f * v + d;
				ap[s][i].push(v + 0.6f * y);
				v = y;
			}
			inS[s] = v;
		}
		float o[REV_LINES];
		for (int i = 0; i < REV_LINES; i++) {
			float v = line[i].readInt(lineLen[i]);
			damp[i] += (v - damp[i]) * dampC;
			float g = std::pow(10.f, -3.f * lineLen[i] / (rt * sr));
			o[i] = damp[i] * g;
		}
		// 8x8 Hadamard
		for (int len = 1; len < REV_LINES; len <<= 1)
			for (int i = 0; i < REV_LINES; i += len << 1)
				for (int j = i; j < i + len; j++) {
					float a = o[j], b = o[j + len];
					o[j] = a + b; o[j + len] = a - b;
				}
		const float norm = 0.35355339f;   // 1 / sqrt(8)
		float wetL = 0.f, wetR = 0.f;
		for (int i = 0; i < REV_LINES; i++) {
			float v = o[i] * norm + (i < 4 ? inS[0] : inS[1]) * 0.5f;
			line[i].push(v);
			float tap = damp[i];
			if (i & 1) wetR += tap; else wetL += tap;
		}
		out[0] = in[0][0] * dA + in[1][0] * dB + wetL * 0.35f;
		out[1] = in[0][1] * dA + in[1][1] * dB + wetR * 0.35f;
	}

	// 10. grains picked at random; the odds of B rise with FADE. Near the
	//     middle each grain also starts from a shuffled point in the recent past.
	void modeGrain(const float in[2][2], float x, float c, float out[2]) {
		if (gPos >= gLen) {
			prevSrc = curSrc; prevOff = curOff;
			float L = grainLen(c) * sr;
			gLen = std::max(32, (int) (L * (0.75f + 0.5f * rnd())));
			gXf = std::max(16, std::min(gLen / 4, (int) (0.005f * sr)));
			curSrc = (x >= 1.f || (x > 0.f && rnd() < x)) ? 1 : 0;
			float sh = 4.f * x * (1.f - x);
			curOff = 1.f + rnd() * L * sh;
			gPos = 0;
		}
		gPos++;
		float t = std::min(1.f, gPos / (float) gXf);
		float gi = t, go = 1.f - t;
		for (int s = 0; s < 2; s++) {
			const Ring& rc = curSrc ? ch[s].hB : ch[s].hA;
			const Ring& rp = prevSrc ? ch[s].hB : ch[s].hA;
			out[s] = rc.read(curOff) * gi + rp.read(prevOff) * go;
		}
	}

	// 11. A freezes into a looping fragment that fades out while B comes in live
	void modeFreeze(const float in[2][2], float x, float c, float out[2]) {
		if (!frozen && x > 0.01f && lf <= 0.f) { frozen = true; frzP = 0.f; }
		if (frozen && x < 0.005f) frozen = false;
		float step = 1.f / (0.01f * sr);
		lf = frozen ? std::min(1.f, lf + step) : std::max(0.f, lf - step);
		bool writing = !frozen && lf <= 0.f;

		float Lp = loopLen(c) * sr;
		float xf = std::min(Lp * 0.25f, 0.02f * sr);
		if (frozen || lf > 0.f) {
			frzP += 1.f;
			if (frzP >= Lp) frzP -= Lp;
			if (frzP >= Lp) frzP = 0.f;
		}
		float ga, gb;
		wide(x, 1.2f, ga, gb);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			if (writing) h.frz.push(in[0][s]);
			float a = in[0][s];
			if (lf > 0.f) {
				float loop = h.frz.read(1.f + Lp - frzP);
				if (frzP > Lp - xf) {
					float t = (frzP - (Lp - xf)) / xf;
					loop = loop * epA(t) + h.frz.read(1.f + 2.f * Lp - frzP) * epB(t);
				}
				a = a + (loop - a) * lf;
			}
			out[s] = a * ga + in[1][s] * gb;
		}
	}

	// 12. first B's dynamics take over A, then B's sound takes over
	void modeEnvSwap(const float in[2][2], float x, float c, float out[2]) {
		float att = coefFor(0.001f, sr), rel = coefFor(envRelease(c), sr);
		float t = clamp01(x * 2.f), u = clamp01(x * 2.f - 1.f);
		for (int s = 0; s < 2; s++) {
			Chan& h = ch[s];
			float ea = h.envA.process(in[0][s], att, rel);
			float eb = h.envB.process(in[1][s], att, rel);
			float g = t > 0.f ? std::pow((eb + 0.02f) / (ea + 0.02f), t) : 1.f;
			g = std::min(g, 10.f);
			float a = in[0][s] * g;
			out[s] = xfade(a, in[1][s], u);
		}
	}

	static inline float folder(float v, float drive) {
		return 3.f * std::sin(v * drive * 0.2f * PI * 0.5f);
	}

	// 13. A wavefolds into noise while B unfolds into clean
	void modeFold(const float in[2][2], float x, float c, float out[2]) {
		float D = foldDrive(c);
		float drA = 1.f + (D - 1.f) * x, drB = 1.f + (D - 1.f) * (1.f - x);
		float wA = clamp01(x * 4.f), wB = clamp01((1.f - x) * 4.f);
		// folded signal is dense, so it sits a little lower
		float ga, gb;
		wide(x, 1.3f, ga, gb);
		for (int s = 0; s < 2; s++) {
			float a = in[0][s] + (folder(in[0][s], drA) - in[0][s]) * wA;
			float b = in[1][s] + (folder(in[1][s], drB) - in[1][s]) * wB;
			out[s] = a * ga + b * gb;
		}
	}

	// 14. the classic: CHARACTER goes linear -> equal power -> wide -> cut
	void modePower(const float in[2][2], float x, float c, float out[2]) {
		float ga, gb;
		if (c < 0.4f) {
			float t = c / 0.4f;
			ga = (1.f - x) * (1.f - t) + epA(x) * t;
			gb = x * (1.f - t) + epB(x) * t;
		}
		else {
			float k = 1.f + 7.f * ((c - 0.4f) / 0.6f);
			wide(x, k, ga, gb);
		}
		for (int s = 0; s < 2; s++)
			out[s] = in[0][s] * ga + in[1][s] * gb;
	}
};

} // namespace morphader
