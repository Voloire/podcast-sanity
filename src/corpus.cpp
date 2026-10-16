#include "sanity/corpus.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

#include "sanity/audio.hpp"
#include "sanity/dsp.hpp"

namespace sanity {

namespace {

// ---------------------------------------------------------------- randomness
// Keep the reference random stream stable on MSVC too. Standard distributions
// use implementation-dependent algorithms. Explicit sampling avoids changing
// the test material with the standard library (libm rounding can still differ).
class Random {
public:
    explicit Random(unsigned seed) : rng_(seed) {}

    // uniform_real_distribution<double>(0, 1): generate_canonical over 53 bits,
    // which costs two mt19937 draws and one division by 2^64.
    double uniform() {
        // Sequenced on purpose: the first draw is the low half of the result,
        // and the order of the two calls must not depend on the compiler.
        const double low = static_cast<double>(rng_());
        const double high = static_cast<double>(rng_());
        double r = (low + high * 4294967296.0) / 18446744073709551616.0;  // 2^32, 2^64
        if (r >= 1.0) r = std::nextafter(1.0, 0.0);
        return r;
    }

    // normal_distribution<double>(0, 1): Marsaglia polar method with the spare
    // value kept, in the same order libstdc++ returns and stores them.
    double normal() {
        if (have_saved_) {
            have_saved_ = false;
            return saved_;
        }
        double x = 0.0, y = 0.0, r2 = 0.0;
        do {
            x = 2.0 * uniform() - 1.0;
            y = 2.0 * uniform() - 1.0;
            r2 = x * x + y * y;
        } while (r2 > 1.0 || r2 == 0.0);
        const double mult = std::sqrt(-2.0 * std::log(r2) / r2);
        saved_ = x * mult;
        have_saved_ = true;
        return y * mult;
    }

    // uniform_int_distribution<std::size_t>(a, b). mt19937 covers exactly 2^32
    // values, which is the case libstdc++ answers with Lemire's nearly
    // divisionless multiplication instead of rejection on a division.
    std::size_t between(std::size_t a, std::size_t b) {
        if (b - a >= UINT32_MAX) {
            const std::uint64_t span = static_cast<std::uint64_t>(b - a) + 1;
            std::uint64_t value;
            const std::uint64_t threshold = span ? (0ULL - span) % span : 0;
            do {
                const std::uint64_t high = rng_();
                value = (high << 32) | rng_();
            } while (value < threshold);
            return a + static_cast<std::size_t>(span ? value % span : value);
        }
        const std::uint32_t range = static_cast<std::uint32_t>(b - a + 1);
        std::uint64_t product = static_cast<std::uint64_t>(rng_()) * range;
        std::uint32_t low = static_cast<std::uint32_t>(product);
        if (low < range) {
            const std::uint32_t threshold = static_cast<std::uint32_t>(0u - range) % range;
            while (low < threshold) {
                product = static_cast<std::uint64_t>(rng_()) * range;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<std::size_t>(product >> 32) + a;
    }

private:
    std::mt19937 rng_;
    bool have_saved_ = false;
    double saved_ = 0.0;
};

// ---------------------------------------------------------------- synthesis
// A two pole resonator used as a formant.
struct Resonator {
    double f = 500, bw = 80, fs = 48000;
    double y1 = 0, y2 = 0;
    void set(double freq, double bandwidth) {
        f = freq;
        bw = bandwidth;
    }
    inline double process(double x) {
        const double r = std::exp(-kPi * bw / fs);
        const double w0 = kTwoPi * f / fs;
        const double a1 = -2.0 * r * std::cos(w0);
        const double a2 = r * r;
        const double gain = (1.0 - r) * std::sqrt(1.0 - a1 * r * r / (4.0) * 0.0 + 0.0);
        const double y = gain * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Glottal {
    double fs = 48000;
    double phase = 0.0;
    double f0 = 120.0;
    // Rosenberg pulse, evaluated by phase accumulation.
    inline double process() {
        const double inc = f0 / fs;
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        const double t = phase;
        const double tp = 0.4, tn = 0.16;
        double v;
        if (t < tp) {
            const double u = t / tp;
            v = 3 * u * u - 2 * u * u * u;
        } else if (t < tp + tn) {
            const double u = (t - tp) / tn;
            v = 1 - u * u;
        } else {
            v = 0.0;
        }
        // Differentiate to get the glottal flow derivative (a realistic source).
        const double d = v - last;
        last = v;
        return d;
    }
    double last = 0.0;
};

void fft_convolve(const std::vector<float>& x, const std::vector<float>& h,
                  std::vector<float>& out) {
    const int block = 2048;
    int nfft = 1;
    while (nfft < block + static_cast<int>(h.size())) nfft <<= 1;
    Fft fft(nfft);
    std::vector<std::complex<float>> H(nfft);
    for (std::size_t i = 0; i < h.size(); i++) H[i] = std::complex<float>(h[i], 0.0f);
    fft.forward(H);
    out.assign(x.size() + h.size(), 0.0f);
    std::vector<std::complex<float>> buf(nfft);
    for (std::size_t off = 0; off < x.size(); off += block) {
        const std::size_t n = std::min<std::size_t>(block, x.size() - off);
        std::fill(buf.begin(), buf.end(), std::complex<float>(0.0f, 0.0f));
        for (std::size_t i = 0; i < n; i++) buf[i] = std::complex<float>(x[off + i], 0.0f);
        fft.forward(buf);
        for (int i = 0; i < nfft; i++) buf[i] *= H[i];
        fft.inverse(buf);
        for (std::size_t i = 0; i < static_cast<std::size_t>(nfft) && off + i < out.size(); i++)
            out[off + i] += buf[i].real();
    }
    out.resize(x.size());
}

// Vowel formant table (F1, F2, F3) close to measured male averages.
struct Vowel {
    double f1, f2, f3;
};
Vowel vowel_of(int idx) {
    switch (idx % 5) {
        case 0: return {730, 1090, 2440};  // a
        case 1: return {530, 1840, 2480};  // e
        case 2: return {270, 2290, 3010};  // i
        case 3: return {570, 840, 2410};   // o
        default: return {300, 870, 2240};  // u
    }
}
Vowel schwa() { return {500, 1500, 2500}; }

}  // namespace

Degradations degradation_studio() {
    Degradations d;
    d.noise_snr_db = 42;
    d.hum_freq = 0;
    d.rt60 = 0;
    d.seed = 11;
    return d;
}

Degradations degradation_home() {
    Degradations d;
    d.noise_snr_db = 26;
    d.noise_tilt = 0.4;
    d.hum_freq = 50;
    d.hum_db = -52;
    d.rt60 = 0.25;
    d.seed = 22;
    return d;
}

Degradations degradation_rough() {
    Degradations d;
    d.noise_snr_db = 14;
    d.noise_tilt = 0.5;
    d.hum_freq = 60;
    d.hum_db = -44;
    d.rt60 = 0.55;
    d.clip_at = 0.6;
    d.clicks = 12;
    d.dropout_per_min = 2;
    d.seed = 33;
    return d;
}

CorpusItem make_corpus_talk(int sample_rate, double seconds, const Degradations& d, unsigned seed,
                            const std::string& name, bool female) {
    CorpusItem item;
    item.name = name;
    item.sample_rate = sample_rate;
    item.degradations = d;

    const std::size_t total = static_cast<std::size_t>(seconds * sample_rate);
    // Two streams: voiced (glottal pulses through the formant bank) and unvoiced
    // (consonant noise). Keeping them apart lets the generator set a realistic
    // level difference, which is what makes the result actually look like speech
    // to the pitch detector.
    std::vector<float> voiced(total, 0.0f);
    std::vector<float> unvoiced(total, 0.0f);
    Random rnd(seed);

    const double f0_base = female ? 205.0 : 122.0;
    Glottal glottal;
    glottal.fs = sample_rate;
    Resonator r1, r2, r3;
    r1.fs = r2.fs = r3.fs = sample_rate;

    std::size_t pos = static_cast<std::size_t>(0.2 * sample_rate);
    int syllable = 0;
    double t_global = 0.0;
    // Build alternating material: a phrase of words, then maybe a hesitation.
    while (pos + static_cast<std::size_t>(0.5 * sample_rate) < total) {
        const bool make_filler = d.filler_every > 0 && (syllable % d.filler_every == d.filler_every - 4);
        if (make_filler) {
            // A hesitation is isolated between two pauses, the way a speaker
            // actually stops and searches for a word.
            pos += static_cast<std::size_t>((0.25 + 0.15 * rnd.uniform()) * sample_rate);
            // One sustained vowel, flat pitch, no consonants.
            const double dur = 0.30 + 0.20 * rnd.uniform();
            const std::size_t n = static_cast<std::size_t>(dur * sample_rate);
            const std::size_t start = pos;
            const Vowel v = schwa();
            r1.set(v.f1, 80);
            r2.set(v.f2, 100);
            r3.set(v.f3, 140);
            glottal.f0 = f0_base;
            glottal.phase = 0.0;
            for (std::size_t i = 0; i < n; i++) {
                const double t = static_cast<double>(i) / n;
                const double env = std::sin(kHalfPi * std::min(1.0, t * 3.0)) * std::sin(kHalfPi * std::min(1.0, (1.0 - t) * 6.0));
                const double src = glottal.process();
                const double y = r1.process(src) + 0.7 * r2.process(src) + 0.35 * r3.process(src);
                voiced[start + i] += static_cast<float>(y * env);
            }
            item.fillers.push_back({start, std::min(total, start + n)});
            pos += n;
            // A pause after the hesitation.
            pos += static_cast<std::size_t>((0.25 + 0.15 * rnd.uniform()) * sample_rate);
            syllable++;
            continue;
        }

        // A word: two or three syllables with a consonant onset.
        const int syllables = 2 + static_cast<int>(rnd.uniform() * 2.0);
        const std::size_t word_start = pos;
        double pitch = f0_base * (0.94 + 0.14 * rnd.uniform());
        for (int s = 0; s < syllables; s++) {
            // Consonant: a short burst of shaped noise.
            const double cons_dur = 0.045 + 0.03 * rnd.uniform();
            const std::size_t nc = static_cast<std::size_t>(cons_dur * sample_rate);
            for (std::size_t i = 0; i < nc && pos + i < total; i++) {
                const double t = static_cast<double>(i) / nc;
                const double env = std::sin(kPi * t);
                unvoiced[pos + i] += static_cast<float>((rnd.uniform() * 2.0 - 1.0) * env);
            }
            pos += nc;

            // Vowel.
            const Vowel v = vowel_of(syllable + s);
            const double vow_dur = 0.10 + 0.10 * rnd.uniform();
            const std::size_t nv = static_cast<std::size_t>(vow_dur * sample_rate);
            r1.set(v.f1, 70);
            r2.set(v.f2, 90);
            r3.set(v.f3, 130);
            glottal.f0 = pitch * (1.0 + 0.03 * (s % 2 ? 1 : -1));  // intonation
            for (std::size_t i = 0; i < nv && pos + i < total; i++) {
                const double t = static_cast<double>(i) / nv;
                const double env = std::sin(kHalfPi * std::min(1.0, t * 4.0)) * std::sin(kHalfPi * std::min(1.0, (1.0 - t) * 4.0));
                const double src = glottal.process();
                const double y = r1.process(src) + 0.7 * r2.process(src) + 0.35 * r3.process(src);
                voiced[pos + i] += static_cast<float>(y * env);
            }
            pos += nv;
        }
        item.words.push_back({word_start, std::min(total, pos)});
        // Inter word pause, occasionally longer for a phrase break.
        const double pause = (syllable % 7 == 6) ? 0.35 + 0.2 * rnd.uniform() : 0.05 + 0.06 * rnd.uniform();
        pos += static_cast<std::size_t>(pause * sample_rate);
        syllable++;
        t_global += 0.4;
    }
    (void)t_global;

    // Shape the consonants like fricatives (energy above 3.5 kHz) and set the
    // level relation between voiced and unvoiced material: consonants sit about
    // 13 dB below the vowels, which is what real close-miked speech does.
    {
        Biquad hp = Biquad::highpass(sample_rate, 3500.0, 0.707);
        biquad_apply_zero_phase(hp, unvoiced);
    }
    {
        double pv = 0.0, pu = 0.0;
        for (float v : voiced) pv = std::max(pv, std::fabs(static_cast<double>(v)));
        for (float v : unvoiced) pu = std::max(pu, std::fabs(static_cast<double>(v)));
        const double gv = pv > 0 ? 0.6 / pv : 0.0;
        const double gu = pu > 0 ? (0.6 * db_to_gain(-13.0)) / pu : 0.0;
        for (std::size_t i = 0; i < total; i++)
            voiced[i] = static_cast<float>(voiced[i] * gv + unvoiced[i] * gu);
    }

    // Normalise the clean voice to a comfortable speech level.
    std::vector<float>& clean = voiced;
    {
        double peak = 0.0;
        for (float v : clean) peak = std::max(peak, std::fabs(static_cast<double>(v)));
        if (peak > 0) {
            const double g = 0.35 / peak;
            for (float& v : clean) v = static_cast<float>(v * g);
        }
    }
    item.clean = clean;

    // ------------------------------------------------------------ degrade
    std::vector<float> x = clean;

    if (d.rt60 > 0.0) {
        // Synthetic exponentially decaying noise impulse response.
        const std::size_t ir_len = static_cast<std::size_t>(d.rt60 * 1.4 * sample_rate);
        std::vector<float> ir(ir_len);
        Random ir_rng(seed + 99);
        for (std::size_t i = 0; i < ir_len; i++) {
            const double t = static_cast<double>(i) / sample_rate;
            ir[i] = static_cast<float>(ir_rng.normal() * std::exp(-6.907755 * t / d.rt60));
        }
        double e = 0.0;
        for (float v : ir) e += static_cast<double>(v) * v;
        const double norm = std::sqrt(e);
        for (float& v : ir) v = static_cast<float>(v / norm);
        std::vector<float> wet;
        fft_convolve(x, ir, wet);
        // Mix mostly direct sound: the direct part is the original signal.
        for (std::size_t i = 0; i < x.size(); i++)
            x[i] = static_cast<float>(x[i] + wet[i] * 0.7);
    }

    // Speech active level for the noise scaling.
    double sp = 0.0;
    long long cnt = 0;
    for (float v : clean)
        if (std::fabs(v) > 1e-4) {
            sp += static_cast<double>(v) * v;
            cnt++;
        }
    const double speech_rms = cnt ? std::sqrt(sp / cnt) : 1e-3;
    item.speech_level_db = 20.0 * std::log10(speech_rms);

    if (d.noise_snr_db > 0.0) {
        const double noise_rms = speech_rms / db_to_gain(d.noise_snr_db);
        Random nr(seed + 7);
        // One pole low pass for a slightly pink spectrum.
        double lp = 0.0;
        const double a = std::min(0.99, std::max(0.0, d.noise_tilt));
        std::vector<float> n(x.size());
        for (std::size_t i = 0; i < n.size(); i++) {
            const double w = nr.normal();
            lp = a * lp + (1.0 - a) * w;
            n[i] = static_cast<float>(w * (1.0 - a) + lp * a * 3.0);
        }
        double e = 0.0;
        for (float v : n) e += static_cast<double>(v) * v;
        const double cur = std::sqrt(e / static_cast<double>(n.size()));
        const double g = noise_rms / std::max(1e-12, cur);
        for (std::size_t i = 0; i < x.size(); i++) x[i] = static_cast<float>(x[i] + n[i] * g);
        item.measured_noise_db = 20.0 * std::log10(noise_rms);
    }

    if (d.hum_freq > 0.0) {
        const double amp = db_to_gain(d.hum_db);
        for (std::size_t i = 0; i < x.size(); i++) {
            const double t = static_cast<double>(i) / sample_rate;
            const double v = std::sin(kTwoPi * d.hum_freq * t) +
                             0.5 * std::sin(kTwoPi * 2 * d.hum_freq * t) +
                             0.25 * std::sin(kTwoPi * 3 * d.hum_freq * t);
            x[i] = static_cast<float>(x[i] + v * amp / 1.25);
        }
        item.measured_hum_db = d.hum_db;
    }

    if (d.clip_at > 0.0) {
        for (float& v : x) v = std::max(-static_cast<float>(d.clip_at), std::min(static_cast<float>(d.clip_at), v));
    }
    if (d.dc != 0.0)
        for (float& v : x) v = static_cast<float>(v + d.dc);

    if (d.clicks > 0) {
        Random cr(seed + 5);
        for (int i = 0; i < d.clicks; i++) {
            const std::size_t p = cr.between(1, x.size() - 2);
            x[p] = (i % 2 == 0) ? 0.95f : -0.95f;
        }
    }
    if (d.dropout_per_min > 0.0) {
        const int count = static_cast<int>(d.dropout_per_min * seconds / 60.0) + 1;
        Random dr(seed + 6);
        for (int i = 0; i < count; i++) {
            const std::size_t p = dr.between(1, x.size() - 1);
            const std::size_t len = static_cast<std::size_t>(0.02 * sample_rate);
            for (std::size_t k = 0; k < len && p + k < x.size(); k++) x[p + k] = 0.0f;
        }
    }

    item.degraded = x;
    return item;
}

}  // namespace sanity
