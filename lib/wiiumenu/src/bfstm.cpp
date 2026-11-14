#include "wiiumenu/bfstm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace wiiumenu::bfstm {
namespace {

std::uint16_t le16(ByteView d, std::size_t o) { return std::uint16_t(d[o] | d[o + 1] << 8); }
std::uint32_t le32(ByteView d, std::size_t o) { return std::uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | std::uint32_t(d[o + 3]) << 24); }
void put16(Bytes& b, std::size_t o, std::uint16_t v) { b[o] = std::uint8_t(v >> 8); b[o + 1] = std::uint8_t(v); }
void put32(Bytes& b, std::size_t o, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[o + i] = std::uint8_t(v >> (24 - 8 * i)); }
std::int32_t s32be(ByteView d, std::size_t o) { return std::int32_t(read_u32(d, o, true)); }
std::size_t align(std::size_t v, std::size_t a) { return (v + a - 1) / a * a; }

std::int16_t clamp16(std::int64_t v) { return std::int16_t(std::clamp<std::int64_t>(v, -32768, 32767)); }

// One DSP-ADPCM sample: nibble n (-8..7), scale s, coefficients c1 c2, history h1 h2.
std::int16_t dsp_sample(int n, int s, int c1, int c2, int h1, int h2) {
    const std::int64_t v = (std::int64_t(n) * (std::int64_t(1) << s) << 11) + 1024 + std::int64_t(c1) * h1 + std::int64_t(c2) * h2;
    return clamp16(v >> 11);
}

int nearest_nibble(int x, int s, std::int64_t base) {
    // want ((n << s) << 11) + 1024 + base  ~=  x << 11
    const std::int64_t num = (std::int64_t(x) << 11) - 1024 - base, d = std::int64_t(1) << (s + 11);
    const std::int64_t q = num >= 0 ? (num + d / 2) / d : -((-num + d / 2) / d);
    return int(std::clamp<std::int64_t>(q, -8, 7));
}

// 8 predictor pairs: least-squares 2-tap predictor per 14-sample frame, clustered with weighted k-means.
void choose_coefs(const std::vector<std::int16_t>& pcm, std::int16_t out[16]) {
    struct P { double a1, a2, w; };
    std::vector<P> pts;
    const std::size_t n = pcm.size();
    auto at = [&](std::ptrdiff_t i) -> double { return i < 0 || std::size_t(i) >= n ? 0.0 : double(pcm[std::size_t(i)]); };
    for (std::size_t s = 0; s < n; s += 14) {
        double r11 = 0, r22 = 0, r12 = 0, q1 = 0, q2 = 0, e = 0;
        for (int t = 0; t < 14; ++t) {
            const auto i = std::ptrdiff_t(s) + t;
            const double x = at(i), x1 = at(i - 1), x2 = at(i - 2);
            r11 += x1 * x1; r22 += x2 * x2; r12 += x1 * x2; q1 += x * x1; q2 += x * x2; e += x * x;
        }
        const double det = r11 * r22 - r12 * r12;
        if (e < 14.0 * 16 * 16 || det <= 1e-6 * (r11 * r22 + 1)) continue;   // silent or degenerate frame
        double a1 = (q1 * r22 - q2 * r12) / det, a2 = (q2 * r11 - q1 * r12) / det;
        a1 = std::clamp(a1, -1.999, 1.999); a2 = std::clamp(a2, -0.999, 0.999);
        pts.push_back({a1, a2, std::sqrt(e)});
    }
    double c[8][2] = {{0, 0}, {1, 0}, {2, -1}, {1.5, -0.6}, {0.5, 0}, {1.8, -0.85}, {1.2, -0.3}, {1.9, -0.95}};
    if (pts.size() >= 8) {
        // k-means++ seeding (fixed seed: same input, same file)
        std::mt19937 rng(12345);
        std::vector<double> d2(pts.size(), 1e300);
        std::size_t first = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) if (pts[i].w > pts[first].w) first = i;
        c[0][0] = pts[first].a1; c[0][1] = pts[first].a2;
        for (int k = 1; k < 8; ++k) {
            double total = 0;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const double dx = pts[i].a1 - c[k - 1][0], dy = pts[i].a2 - c[k - 1][1];
                d2[i] = std::min(d2[i], (dx * dx + dy * dy) * pts[i].w);
                total += d2[i];
            }
            std::uniform_real_distribution<double> u(0, total);
            double pick = u(rng), acc = 0;
            std::size_t sel = pts.size() - 1;
            for (std::size_t i = 0; i < pts.size(); ++i) { acc += d2[i]; if (acc >= pick) { sel = i; break; } }
            c[k][0] = pts[sel].a1; c[k][1] = pts[sel].a2;
        }
        std::vector<int> owner(pts.size());
        for (int iter = 0; iter < 40; ++iter) {
            double sum[8][3] = {};
            for (std::size_t i = 0; i < pts.size(); ++i) {
                int best = 0; double bd = 1e300;
                for (int k = 0; k < 8; ++k) {
                    const double dx = pts[i].a1 - c[k][0], dy = pts[i].a2 - c[k][1], dd = dx * dx + dy * dy;
                    if (dd < bd) { bd = dd; best = k; }
                }
                owner[i] = best; sum[best][0] += pts[i].a1 * pts[i].w; sum[best][1] += pts[i].a2 * pts[i].w; sum[best][2] += pts[i].w;
            }
            for (int k = 0; k < 8; ++k)
                if (sum[k][2] > 0) { c[k][0] = sum[k][0] / sum[k][2]; c[k][1] = sum[k][1] / sum[k][2]; }
        }
    }
    for (int k = 0; k < 8; ++k) {
        out[2 * k] = clamp16(std::llround(c[k][0] * 2048));
        out[2 * k + 1] = clamp16(std::llround(c[k][1] * 2048));
    }
}

}  // namespace

Pcm read_wav(ByteView d) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) throw FormatError("not a WAV file");
    std::size_t o = 12, data_off = 0, data_len = 0;
    int fmt = 0, ch = 0, bits = 0;
    std::uint32_t rate = 0;
    while (o + 8 <= d.size()) {
        const std::uint32_t len = le32(d, o + 4);
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0) {
            fmt = le16(d, o + 8); ch = le16(d, o + 10); rate = le32(d, o + 12); bits = le16(d, o + 22);
            if (fmt == 0xFFFE && len >= 40) fmt = le16(d, o + 8 + 24);  // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts with the format code
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            data_off = o + 8; data_len = std::min<std::size_t>(len, d.size() - data_off);
        }
        o += 8 + len + (len & 1);
    }
    if (fmt != 1 || (bits != 16 && bits != 24) || ch < 1 || !data_off) throw FormatError("WAV must be 16 or 24-bit PCM");
    Pcm p;
    p.sample_rate = rate;
    const std::size_t bytes = std::size_t(bits / 8), frames = data_len / (bytes * std::size_t(ch));
    p.channels.assign(std::size_t(ch), std::vector<std::int16_t>(frames));
    // 24-bit goes down to 16 with TPDF dither (two uniform randoms of one 16-bit step each), so quiet parts fade
    // into soft noise instead of grainy rounding; the fixed seed means the same song always gives the same file
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> step(0, 255);
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) {
            const std::size_t at = data_off + (i * std::size_t(ch) + std::size_t(c)) * bytes;
            if (bytes == 2) {
                p.channels[std::size_t(c)][i] = std::int16_t(le16(d, at));
                continue;
            }
            const std::int32_t s24 = std::int32_t(std::uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16) << 8) >> 8;
            const std::int64_t dithered = std::int64_t(s24) + step(rng) - step(rng) + 128;  // + 128: round to nearest
            p.channels[std::size_t(c)][i] = clamp16(dithered >> 8);
        }
    return p;
}

AdpcmChannel encode_adpcm(const std::vector<std::int16_t>& pcm, std::uint32_t loop_start, std::uint32_t samples_per_block) {
    AdpcmChannel out;
    choose_coefs(pcm, out.coefs);
    const std::size_t n = pcm.size(), frames = (n + 13) / 14;
    out.data.assign(frames * 8, 0);
    int h1 = 0, h2 = 0;
    for (std::size_t f = 0; f < frames; ++f) {
        const std::size_t start = f * 14;
        if (start % samples_per_block == 0) out.block_history.push_back({std::int16_t(h1), std::int16_t(h2)});
        int x[14];
        for (int t = 0; t < 14; ++t) x[t] = start + std::size_t(t) < n ? pcm[start + std::size_t(t)] : 0;
        int best_c = 0, best_s = 0;
        std::int64_t best_err = INT64_MAX;
        for (int ci = 0; ci < 8; ++ci) {
            const int c1 = out.coefs[2 * ci], c2 = out.coefs[2 * ci + 1];
            // scale estimate from the open-loop residual, then try it and its neighbours
            double emax = 0;
            for (int t = 0; t < 14; ++t) {
                const double prev1 = t >= 1 ? x[t - 1] : h1;
                const double prev2 = t >= 2 ? x[t - 2] : (t == 1 ? h1 : h2);
                emax = std::max(emax, std::fabs(x[t] - (c1 * prev1 + c2 * prev2) / 2048.0));
            }
            int s0 = 0;
            while (s0 < 12 && 7.5 * double(1 << s0) < emax) ++s0;
            for (int s = std::max(0, s0 - 1); s <= std::min(12, s0 + 1); ++s) {
                int a = h1, b = h2;
                std::int64_t err = 0;
                for (int t = 0; t < 14 && err < best_err; ++t) {
                    const std::int64_t base = std::int64_t(c1) * a + std::int64_t(c2) * b;
                    const int nib = nearest_nibble(x[t], s, base);
                    const int y = dsp_sample(nib, s, c1, c2, a, b);
                    err += std::int64_t(x[t] - y) * (x[t] - y);
                    b = a; a = y;
                }
                if (err < best_err) { best_err = err; best_c = ci; best_s = s; }
            }
        }
        const int c1 = out.coefs[2 * best_c], c2 = out.coefs[2 * best_c + 1];
        std::uint8_t* fr = &out.data[f * 8];
        fr[0] = std::uint8_t(best_c << 4 | best_s);
        if (f == 0) out.pred_scale = fr[0];
        if (start == loop_start) { out.loop_pred_scale = fr[0]; out.loop_hist1 = std::int16_t(h1); out.loop_hist2 = std::int16_t(h2); }
        for (int t = 0; t < 14; ++t) {
            const std::int64_t base = std::int64_t(c1) * h1 + std::int64_t(c2) * h2;
            const int nib = nearest_nibble(x[t], best_s, base);
            const int y = dsp_sample(nib, best_s, c1, c2, h1, h2);
            fr[1 + t / 2] |= std::uint8_t((nib & 15) << (t % 2 ? 0 : 4));
            h2 = h1; h1 = y;
        }
    }
    return out;
}

std::vector<std::int16_t> decode_adpcm(ByteView data, const std::int16_t coefs[16], std::size_t samples) {
    std::vector<std::int16_t> out(samples);
    int h1 = 0, h2 = 0;
    for (std::size_t i = 0; i < samples; ++i) {
        const std::size_t f = i / 14, t = i % 14;
        const std::uint8_t hdr = data[f * 8];
        const int c = hdr >> 4, s = hdr & 15;
        int nib = (data[f * 8 + 1 + t / 2] >> (t % 2 ? 0 : 4)) & 15;
        if (nib >= 8) nib -= 16;
        const int y = dsp_sample(nib, s, coefs[2 * c], coefs[2 * c + 1], h1, h2);
        out[i] = std::int16_t(y); h2 = h1; h1 = y;
    }
    return out;
}

namespace {

// What write and write_adpcm take from the template stream.
struct Template {
    std::size_t info_off = 0, info_size = 0, data_off = 0, si = 0, channels = 0;
    std::uint32_t block_size = 0, spb = 0, data_start = 0;
};

Template read_template(ByteView tpl) {
    if (tpl.size() < 0x40 || std::memcmp(tpl.data(), "FSTM", 4) != 0) throw FormatError("template is not a BFSTM");
    Template t;
    const std::uint16_t nblocks = read_u16(tpl, 0x10, true);
    for (std::uint16_t i = 0; i < nblocks; ++i) {
        const std::uint16_t type = read_u16(tpl, 0x14 + 12 * std::size_t(i), true);
        const std::uint32_t off = read_u32(tpl, 0x18 + 12 * std::size_t(i), true), size = read_u32(tpl, 0x1C + 12 * std::size_t(i), true);
        if (type == 0x4000) { t.info_off = off; t.info_size = size; }
        if (type == 0x4002) t.data_off = off;
    }
    if (!t.info_off || !t.data_off || t.info_off + t.info_size > tpl.size()) throw FormatError("template has no INFO or DATA block");
    const ByteView info = tpl.subspan(t.info_off, t.info_size);
    const std::size_t body = 8;
    t.si = body + std::size_t(s32be(info, body + 4));
    if (info[t.si] != 2) throw FormatError("template codec is not DSP-ADPCM");
    t.channels = info[t.si + 2];
    t.block_size = read_u32(info, t.si + 0x14, true);
    t.spb = read_u32(info, t.si + 0x18, true);
    t.data_start = read_u32(info, t.si + 0x34, true);  // sample data offset inside DATA (after its 8-byte header)
    return t;
}

Bytes assemble(ByteView tpl, const Template& t, const std::vector<AdpcmChannel>& enc, std::size_t n, std::uint32_t sample_rate,
               const Options& opt);

}  // namespace

Bytes write(ByteView tpl, const Pcm& audio, const Options& opt, double* snr_db) {
    const Template t = read_template(tpl);
    const std::size_t channels = t.channels;
    if (audio.channels.size() != channels) throw FormatError("audio has " + std::to_string(audio.channels.size()) + " channels, template has " + std::to_string(channels));
    const std::size_t n = audio.channels[0].size();
    if (opt.loop_start % t.spb != 0 || opt.loop_start >= n) throw FormatError("loop start must be a multiple of " + std::to_string(t.spb) + " and inside the audio");

    // encode (identical channels are encoded once)
    std::vector<AdpcmChannel> enc(channels);
    double sig = 0, noise = 0;
    for (std::size_t c = 0; c < channels; ++c) {
        std::size_t same = c;
        for (std::size_t p = 0; p < c; ++p) if (audio.channels[p] == audio.channels[c]) { same = p; break; }
        if (same != c) { enc[c] = enc[same]; continue; }
        enc[c] = encode_adpcm(audio.channels[c], opt.loop_start, t.spb);
        const auto back = decode_adpcm(enc[c].data, enc[c].coefs, n);
        for (std::size_t i = 0; i < n; ++i) { const double a = audio.channels[c][i], e = a - back[i]; sig += a * a; noise += e * e; }
    }
    if (snr_db) *snr_db = noise > 0 ? 10 * std::log10(sig / noise) : 999;
    return assemble(tpl, t, enc, n, audio.sample_rate, opt);
}

Bytes write_adpcm(ByteView tpl, const std::vector<AdpcmChannel>& audio, std::size_t n, std::uint32_t sample_rate, const Options& opt) {
    const Template t = read_template(tpl);
    if (audio.empty()) throw FormatError("no DSP-ADPCM channels");
    if (opt.loop_start % t.spb != 0 || opt.loop_start >= n) throw FormatError("loop start must be a multiple of " + std::to_string(t.spb) + " and inside the audio");
    // each channel's decoder history at every block start (for SEEK) and at the loop start: run the decoder over it
    std::vector<AdpcmChannel> done;
    for (const AdpcmChannel& in : audio) {
        if (!n || in.data.size() < (n + 13) / 14 * 8) throw FormatError("DSP-ADPCM data is shorter than its sample count");
        AdpcmChannel ch = in;
        ch.block_history.clear();
        int h1 = in.hist1, h2 = in.hist2;
        for (std::size_t f = 0, frames = (n + 13) / 14; f < frames; ++f) {
            const std::size_t start = f * 14;
            if (start % t.spb == 0) ch.block_history.push_back({std::int16_t(h1), std::int16_t(h2)});
            const std::uint8_t hdr = ch.data[f * 8];
            if (start == opt.loop_start) { ch.loop_pred_scale = hdr; ch.loop_hist1 = std::int16_t(h1); ch.loop_hist2 = std::int16_t(h2); }
            const int c = hdr >> 4, s = hdr & 15;
            if (c > 7) throw FormatError("DSP-ADPCM frame " + std::to_string(f) + " has a bad predictor");
            for (std::size_t i = 0; i < 14 && start + i < n; ++i) {
                int nib = (ch.data[f * 8 + 1 + i / 2] >> (i % 2 ? 0 : 4)) & 15;
                if (nib >= 8) nib -= 16;
                const int y = dsp_sample(nib, s, ch.coefs[2 * c], ch.coefs[2 * c + 1], h1, h2);
                h2 = h1; h1 = y;
            }
        }
        ch.pred_scale = ch.data[0];
        done.push_back(std::move(ch));
    }
    std::vector<AdpcmChannel> enc;
    for (std::size_t c = 0; c < t.channels; ++c) enc.push_back(done[c % done.size()]);
    return assemble(tpl, t, enc, n, sample_rate, opt);
}

namespace {

Bytes assemble(ByteView tpl, const Template& t, const std::vector<AdpcmChannel>& enc, std::size_t n, std::uint32_t sample_rate,
               const Options& opt) {
    Bytes info(tpl.begin() + std::ptrdiff_t(t.info_off), tpl.begin() + std::ptrdiff_t(t.info_off + t.info_size));
    const std::size_t body = 8, si = t.si, channels = t.channels;
    const std::uint32_t block_size = t.block_size, spb = t.spb, data_start = t.data_start;
    const std::uint16_t nblocks = read_u16(tpl, 0x10, true);
    const std::size_t blocks = (n + spb - 1) / spb, last_samples = n - (blocks - 1) * spb;
    const std::size_t last_full = last_samples / 14, last_rest = last_samples % 14;
    const std::size_t last_size = last_full * 8 + (last_rest ? 1 + (last_rest + 1) / 2 : 0), last_padded = align(last_size, 0x20);

    // INFO: template with this stream's values
    info[si + 1] = opt.loop ? 1 : 0;
    put32(info, si + 0x04, sample_rate);
    put32(info, si + 0x08, opt.loop_start);
    put32(info, si + 0x0C, std::uint32_t(n));
    put32(info, si + 0x10, std::uint32_t(blocks));
    put32(info, si + 0x1C, std::uint32_t(last_size));
    put32(info, si + 0x20, std::uint32_t(last_samples));
    put32(info, si + 0x24, std::uint32_t(last_padded));
    const std::size_t ct = body + std::size_t(s32be(info, body + 20));
    if (read_u32(info, ct, true) != channels) throw FormatError("template channel table does not match its channel count");
    for (std::size_t c = 0; c < channels; ++c) {
        const std::size_t e = ct + std::size_t(s32be(info, ct + 8 + 8 * c));
        const std::size_t a = e + std::size_t(s32be(info, e + 4));
        for (int k = 0; k < 16; ++k) put16(info, a + 2 * std::size_t(k), std::uint16_t(enc[c].coefs[k]));
        put16(info, a + 32, 0);  // gain
        put16(info, a + 34, enc[c].pred_scale);
        put16(info, a + 36, std::uint16_t(enc[c].hist1));
        put16(info, a + 38, std::uint16_t(enc[c].hist2));
        put16(info, a + 40, enc[c].loop_pred_scale);
        put16(info, a + 42, std::uint16_t(enc[c].loop_hist1));
        put16(info, a + 44, std::uint16_t(enc[c].loop_hist2));
    }

    // SEEK: decoder history at each block start, [block][channel]{hist1, hist2}. These values are
    // little-endian even in big-endian files (checked against stock BGM_SET_ACS.bfstm).
    Bytes seek(align(8 + blocks * channels * 4, 0x20), 0);
    std::memcpy(seek.data(), "SEEK", 4);
    put32(seek, 4, std::uint32_t(seek.size()));
    auto put16le = [&](std::size_t o, std::int16_t v) { seek[o] = std::uint8_t(std::uint16_t(v)); seek[o + 1] = std::uint8_t(std::uint16_t(v) >> 8); };
    for (std::size_t b = 0; b < blocks; ++b)
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t o = 8 + (b * channels + c) * 4;
            put16le(o, enc[c].block_history[b].first);
            put16le(o + 2, enc[c].block_history[b].second);
        }

    // DATA: blocks interleaved by channel
    Bytes data(8 + data_start, 0);
    std::memcpy(data.data(), "DATA", 4);
    for (std::size_t b = 0; b < blocks; ++b) {
        const bool last = b + 1 == blocks;
        const std::size_t take = last ? last_size : block_size, room = last ? last_padded : block_size;
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t from = b * block_size;
            const std::size_t avail = std::min(take, enc[c].data.size() - std::min(from, enc[c].data.size()));
            data.insert(data.end(), enc[c].data.begin() + std::ptrdiff_t(from), enc[c].data.begin() + std::ptrdiff_t(from + avail));
            data.resize(data.size() + (room - avail), 0);
        }
    }
    put32(data, 4, std::uint32_t(data.size()));

    // header: the template's, with new block positions
    Bytes out(tpl.begin(), tpl.begin() + 0x40);
    const std::size_t seek_off = align(0x40 + info.size(), 0x20), dat_off = align(seek_off + seek.size(), 0x20);
    for (std::uint16_t i = 0; i < nblocks; ++i) {
        const std::size_t r = 0x14 + 12 * std::size_t(i);
        const std::uint16_t type = read_u16(out, r, true);
        if (type == 0x4000) { put32(out, r + 4, 0x40); put32(out, r + 8, std::uint32_t(info.size())); }
        if (type == 0x4001) { put32(out, r + 4, std::uint32_t(seek_off)); put32(out, r + 8, std::uint32_t(seek.size())); }
        if (type == 0x4002) { put32(out, r + 4, std::uint32_t(dat_off)); put32(out, r + 8, std::uint32_t(data.size())); }
    }
    out.insert(out.end(), info.begin(), info.end());
    out.resize(seek_off, 0);
    out.insert(out.end(), seek.begin(), seek.end());
    out.resize(dat_off, 0);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, 0x0C, std::uint32_t(out.size()));
    return out;
}

}  // namespace

}  // namespace wiiumenu::bfstm
