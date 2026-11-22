# Morphader

A stereo audio crossfader for VCV Rack with fourteen different ways to get from **A** to **B**.

FADE moves from A to B, MODE picks the method, and CHARACTER shapes it. At FADE 0 you always hear exactly A, and at FADE 1 exactly B.

| # | Mode | What happens on the way | CHARACTER |
|---|------|-------------------------|-----------|
| 1 | Filter swap | A's low-pass closes while B's high-pass opens | resonance |
| 2 | Gate chop | fast switching; the time share on B follows FADE (50/50 in the middle) | rate, 0.5 Hz to 2 kHz |
| 3 | Ring | A x B ring modulation peaks in the middle | width of the ring zone |
| 4 | Vocoder | A, then A speaking with B's spectrum, then B speaking with A's, then B | envelope release |
| 5 | Spectral | 8 bands; B takes them over from the bottom up | overlap between bands |
| 6 | Tape stop | A slows and drops in pitch, B spins up | how far the pitch drops |
| 7 | Crush | A crushes into grit, B resolves out of it | bits to sample rate |
| 8 | Dub throw | A is thrown into a delay and trails off as B arrives | echo time |
| 9 | Reverb wash | A drowns in reverb, B emerges dry | decay |
| 10 | Grain shuffle | random grains; the odds of B rise with FADE | grain length |
| 11 | Freeze | A freezes into a loop that fades while B comes in live | loop length |
| 12 | Env swap | B's dynamics take over A, then B's sound takes over | envelope release |
| 13 | Fold | A wavefolds into noise, B unfolds into clean | fold amount |
| 14 | Equal power | the classic, for reference | linear, equal power, wide, cut |

**GLIDE** sets how long a full A-to-B trip takes (10 ms to 10 s). **GO** (button or trigger) flips to the other side over the glide time; the light shows it's flipped.

Right inputs are normalled to the left ones, so mono sources work.

Build: every push builds a Mac (Apple Silicon) `.vcvplugin` with GitHub Actions. `make test` runs offline engine checks and writes a demo WAV. The panel comes from `res/make_panel.py`.

## Licence

MIT (see `LICENSE.txt`): you're free to use, change, port (VST/CLAP, other platforms, hardware) and share this code, as long as the copyright notice and licence text come along. Fonts on the panel (Inter, Climate Crisis) are under the SIL Open Font Licence.
