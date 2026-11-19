// Offline checks + demo WAV for the Morphader engine (no Rack needed).
//   make test   (writes test/morphader-demo.wav)
// For every mode: FADE sweeps A -> B over 6 s, holds B for 1 s.
// Checks: output is exactly A at FADE 0, no NaNs, peaks stay sane, and the
// output ends up as B.
#include "../src/MorphaderDSP.hpp"
#include <cstdio>
#include <string>
using namespace morphader;

static void writeWav(const char* path, const std::vector<float>& lr, int sr) {
	FILE* f = fopen(path, "wb");
	if (!f) return;
	int n = (int) lr.size();
	int bytes = n * 2;
	auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
	w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
	fwrite("data", 1, 4, f); w32(bytes);
	for (float v : lr) { float s = std::max(-1.f, std::min(1.f, v / 10.f)); int16_t q = (int16_t) (s * 32767); fwrite(&q, 2, 1, f); }
	fclose(f);
}

int main(int argc, char** argv) {
	std::string dir = argc > 1 ? argv[1] : ".";
	const int SR = 48000;
	std::vector<float> wav;
	int fails = 0;
	uint32_t rs = 1;
	auto noise = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs >> 8) / 8388608.f - 1.f; };
	for (int m = 0; m < NUM_MODES; m++) {
		Engine e;
		e.setSampleRate(SR);
		e.setMode(m); e.mode = m; e.cs = 0.5f;
		float phA1 = 0, phA2 = 0, kph = 0, hpS = 0, lpS = 0;
		double sumErrA = 0, sumErrB = 0, sumB = 0;
		float peak = 0;
		bool nan = false;
		int N = SR * 7.5;
		for (int i = 0; i < N; i++) {
			float t = i / (float) SR;
			// A: two detuned saws (a chord-ish drone)
			phA1 += 110.f / SR; if (phA1 >= 1) phA1 -= 1;
			phA2 += 164.8f / SR; if (phA2 >= 1) phA2 -= 1;
			float A = 2.f * (2 * phA1 - 1) + 2.f * (2 * phA2 - 1);
			// B: kick + hats at 125 bpm
			float beat = std::fmod(t * 125.f / 60.f, 1.f);
			float kenv = std::exp(-beat * 12.f);
			kph += (45.f + 120.f * std::exp(-beat * 40.f)) / SR; if (kph >= 1) kph -= 1;
			float hat = noise(); hpS += (hat - hpS) * 0.3f;
			float off = std::fmod(beat + 0.5f, 1.f);
			float B = 5.f * kenv * std::sin(2 * PI * kph) + 3.f * (hat - hpS) * std::exp(-off * 30.f);
			lpS = B;
			float x = std::min(1.f, std::max(0.f, (t - 0.3f) / 6.f));
			float in[2][2] = {{A, A}, {B, B}};
			float out[2];
			e.process(in, x, 0.5f, out);
			for (int s = 0; s < 2; s++) {
				if (!std::isfinite(out[s])) nan = true;
				peak = std::max(peak, std::fabs(out[s]));
				wav.push_back(out[s]);
			}
			if (t < 0.2f) sumErrA += std::fabs(out[0] - A);
			if (t > 7.1f) { sumErrB += std::fabs(out[0] - B); sumB += std::fabs(B); }
		}
		double errA = sumErrA / (0.2 * SR), errB = sumErrB / std::max(sumB, 1e-9);
		bool ok = !nan && peak < 25.f && errA < 1e-3 && errB < 0.05;
		// dub/wash keep a tail after reaching B; allow it
		if ((m == DUB || m == WASH) && !nan && peak < 25.f && errA < 1e-3 && errB < 0.6) ok = true;
		printf("%-14s peak %6.2f V  errA %.5f  errB %.4f  %s\n", MODE_NAMES[m], peak, errA, errB, ok ? "ok" : "FAIL");
		if (!ok) fails++;
		for (int i = 0; i < SR / 2 * 2; i++) wav.push_back(0.f);
	}
	writeWav((dir + "/test/morphader-demo.wav").c_str(), wav, SR);
	printf(fails ? "%d FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
