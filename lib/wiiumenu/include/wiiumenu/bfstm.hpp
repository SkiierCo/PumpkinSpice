// BFSTM (Cafe stream, version 3.0.0) writer and DSP-ADPCM codec.
//
// The writer copies the layout of an existing stream file (its INFO block is the template) and
// fills in new audio, so the result matches what the console already plays from that archive.
#pragma once

#include <cstdint>
#include <vector>

#include "wiiumenu/bytes.hpp"

namespace wiiumenu::bfstm {

struct Pcm {
    std::uint32_t sample_rate = 0;
    std::vector<std::vector<std::int16_t>> channels;  // one vector per channel, equal lengths
};

// 16 or 24-bit PCM WAV (RIFF/WAVE, format 1, or WAVE_FORMAT_EXTENSIBLE with PCM inside), any channel count.
// 24-bit is dithered down to 16-bit.
Pcm read_wav(ByteView data);

// DSP-ADPCM, one channel.
struct AdpcmChannel {
    std::int16_t coefs[16] = {};
    std::uint16_t pred_scale = 0, loop_pred_scale = 0;
    std::int16_t hist1 = 0, hist2 = 0, loop_hist1 = 0, loop_hist2 = 0;
    Bytes data;                                   // 8 bytes per 14 samples
    std::vector<std::pair<std::int16_t, std::int16_t>> block_history;  // decoder history at each block start
};
AdpcmChannel encode_adpcm(const std::vector<std::int16_t>& pcm, std::uint32_t loop_start, std::uint32_t samples_per_block);
std::vector<std::int16_t> decode_adpcm(ByteView data, const std::int16_t coefs[16], std::size_t samples);

struct Options {
    std::uint32_t loop_start = 0;  // sample index; must be a multiple of the template's samples per block
    bool loop = true;
};

// `channels`: the output channels in order (e.g. L, R, L, R for two stereo tracks). The template's
// channel count, codec (DSP-ADPCM) and block size are kept.
Bytes write(ByteView template_bfstm, const Pcm& audio, const Options& opt, double* snr_db = nullptr);

// Same layout, from audio that is already DSP-ADPCM (e.g. a song stored as a wave in a sound archive), so nothing is
// encoded again and the stream sounds exactly like the wave. Template channel c gets audio[c % audio.size()]: a mono
// wave goes to every channel, a stereo one to L, R, L, R. `samples` is the sample count of each channel's data; the
// block histories and the loop history are worked out from it.
Bytes write_adpcm(ByteView template_bfstm, const std::vector<AdpcmChannel>& audio, std::size_t samples, std::uint32_t sample_rate,
                  const Options& opt);

}  // namespace wiiumenu::bfstm
