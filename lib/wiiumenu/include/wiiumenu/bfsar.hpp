// BFSAR (Cafe sound archive, version 2.0.0): read-only map plus targeted patches.
//
// An archive has three blocks: STRG (names), INFO (tables of sounds, sound sets,
// banks, wave archives, groups, players, files, player limits) and FILE (file data).
// Many files live inside group files (FGRP) instead of directly in FILE.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "wiiumenu/bfstm.hpp"
#include "wiiumenu/bytes.hpp"

namespace wiiumenu::bfsar {

enum class SoundType { Stream, Wave, Sequence, Unknown };

struct Sound {
    std::uint32_t index = 0;
    std::string name;
    SoundType type = SoundType::Unknown;
    std::uint32_t file = 0;
    std::uint32_t player = 0;  // player index
    std::uint8_t volume = 0;
    std::uint32_t wave_index = 0;           // wave sounds: entry in the wave sound data file
    std::vector<std::uint32_t> banks;       // sequence sounds: bank indices
    std::uint32_t track_mask = 0;           // sequence: allocated tracks; stream: valid tracks
    std::uint16_t stream_channels = 0;      // stream sounds
    std::size_t entry_offset = 0;           // absolute offset of the sound info entry
    std::size_t detail_offset = 0;          // absolute offset of the type-specific detail block
};

struct File {
    std::uint32_t index = 0;
    bool external = false;
    std::string path;                  // external files
    std::optional<std::size_t> offset; // internal files stored directly in FILE
    std::uint32_t size = 0;
    std::string magic;
};

struct GroupItem {
    std::uint32_t file = 0;
    std::string magic;
    std::uint32_t size = 0;
    std::uint32_t wave_count = 0;  // FWAR only
};

struct Named { std::uint32_t index = 0; std::string name; std::uint32_t file = 0; };

struct Limits {
    std::uint16_t sequence_sounds = 0, sequence_tracks = 0, stream_sounds = 0, stream_tracks = 0, stream_channels = 0,
                  wave_sounds = 0, wave_tracks = 0;
};

struct Archive {
    std::vector<std::string> strings;
    std::vector<Sound> sounds;
    std::vector<Named> banks, wave_archives, groups;
    std::vector<std::string> players;
    std::vector<File> files;
    std::vector<std::vector<GroupItem>> group_items;  // per group
    Limits limits;
};

bool is_bfsar(ByteView data);
Archive read(ByteView data);
const char* type_name(SoundType t);

// Make `sound` play the same stream as `stream_sound` (same file and stream settings),
// by repointing its entry. Fixed-size patch: nothing else in the archive moves.
Bytes alias_to_stream(ByteView data, const std::string& sound, const std::string& stream_sound);

// Add an external file entry (`path`, relative to the archive's folder, e.g. "stream/BGM_MEN.bfstm")
// and make `sound` a stream sound that plays it with `template_stream`'s stream settings (tracks,
// channels). Sounds in `mute` get volume 0. All existing data keeps its position inside INFO; the
// file table is rewritten at the end of INFO and the FILE block moves by a multiple of 4 KB.
Bytes add_stream(ByteView data, const std::string& sound, const std::string& template_stream, const std::string& path,
                 const std::vector<std::string>& mute);

// Player limits for streams (the Menu reserves stream memory from these at startup).
Bytes set_stream_limits(ByteView data, std::uint16_t stream_sounds, std::uint16_t stream_tracks, std::uint16_t stream_channels);

// Move `sounds` to the existing player `player` (fixed-size patch). A player plays at most its sound count at once
// (sound-map lists them); with 1, each start of a sound on it takes the place of the one playing, instead of layering
// (e.g. a cursor sound repeated quickly). Adding a player instead crashes the Wii U Menu at startup (tested in Cemu),
// so use one no sound plays (in the Menu's archive: PLAYER_BGM_SUB, PLAYER_BGM_FANFARE, 1 sound each).
Bytes set_sound_player(ByteView data, const std::string& player, const std::vector<std::string>& sounds);

// Sets a sound's own volume (the byte the sound archive keeps for it; stock is mostly 127). This is the sound's
// volume control, so the audio data and the stream's track settings stay as they are.
Bytes set_sound_volume(ByteView data, const std::string& sound, std::uint8_t volume);

// Replace wave `wave` of wave archive `wave_archive` (index in the archive's wave archive table; the
// numbers Citric Composer shows) with new audio, stored as DSP-ADPCM (`adpcm`) or PCM16, with the old
// wave's channel count (the audio is downmixed to mono or doubled to stereo to match) and the audio's
// own sample rate and length. The whole group holding the wave archive is loaded into sound memory,
// which it shares with what the player limits reserve (streams); a group that does not fit fails to load
// and its sound effects go silent, so DSP-ADPCM (about 3.5x smaller) is the default. Every copy of the wave archive is
// changed (directly in FILE and inside group files); data after the wave moves by a multiple of
// 32 bytes, and the offsets and sizes in the wave archive, group file and archive are updated.
// Verified by reading back: the new wave is in place and every other wave and file is unchanged.
// `channels_out` 1 or 2 sets the new wave's channel count; 0 keeps the old wave's.
Bytes replace_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, std::uint32_t sample_rate,
                   const std::vector<std::vector<std::int16_t>>& channels, bool adpcm = true, std::uint32_t channels_out = 0);

// Put wave `from_wave` of another archive's wave archive `wave_archive` (as stored, any encoding) in place of
// wave `wave` here. Same checks as replace_wave.
Bytes copy_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, ByteView from, std::uint32_t from_wave);

// Point entry `index` of bank `bank`'s wave id table at wave `wave` of `wave_archive`. Instruments refer to the table
// by index, so this changes which wave they play. A bank stored in several groups has a separate table per copy (each
// group's own wave archive); only copies whose entry plays `wave_archive` change.
Bytes set_bank_wave(ByteView data, std::uint32_t bank, std::uint32_t index, std::uint32_t wave, std::uint32_t wave_archive);

// Set the velocity of the note command at `offset` in the DATA block of sequence file `file` (0 = silent; the note
// still runs, so the sound's timing is unchanged). Changes the copy stored directly in FILE and the copies inside
// `groups` (every group when empty). Sequences share code between sounds (e.g. the Menu's click layered under
// decide, cancel, ...), so one note can belong to several sounds. Fixed-size patch. `count` gets the copies changed.
Bytes set_note_velocity(ByteView data, std::uint32_t file, std::uint32_t offset, std::uint8_t velocity,
                        const std::vector<std::string>& groups, std::size_t* count = nullptr);

// Give every other stored copy of bank `bank` (same number of entries) the wave id table of the copy that plays
// `wave_archive`, so the bank plays the same waves in every group (e.g. the Menu's sound effects also in the
// Account and Settings groups, which otherwise use their own wave archives). `count` gets the copies changed.
Bytes share_bank_table(ByteView data, std::uint32_t bank, std::uint32_t wave_archive, std::size_t* count = nullptr);

// The Menu's music as one stream (docs/STREAMING.md): BGM_MEN_TV streams `path` (relative to the archive's folder) with
// BGM_SET_ACS's stream settings, BGM_MEN_DRC_MAIN (GamePad) plays the same stream, the four layer sequences are muted,
// the stream limits go up to 4 sounds / 4 tracks / 16 channels, the two music sounds get `tv_volume` and `drc_volume`,
// and the waves only BANK_MEN_BGM uses are made silent (GROUP_MEN shares sound memory with the bigger stream buffers;
// with the old song still in it the Menu asserts at boot). The sound effects use other banks, so they stay.
Bytes stream_music(ByteView data, std::uint8_t tv_volume, std::uint8_t drc_volume, const std::string& path = "stream/BGM_MEN.bfstm");

// A wave as it's stored in the archive (first stored copy of the wave archive): its format, rate, length and loop,
// and for DSP-ADPCM every channel's coefficients, start values and sample data as they are.
struct Wave {
    std::uint8_t encoding = 0;  // 0 PCM8, 1 PCM16, 2 DSP-ADPCM
    bool loop = false;
    std::uint32_t sample_rate = 0, loop_start = 0, samples = 0, channels = 0;
    std::vector<bfstm::AdpcmChannel> adpcm;  // one per channel, DSP-ADPCM only (block_history left empty)
};
Wave read_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave);

// Waves (index in `wave_archive`) that bank `bank` plays, from the bank's wave id table.
std::vector<std::uint32_t> bank_waves(ByteView data, std::uint32_t bank, std::uint32_t wave_archive);

// Replace every wave of `wave_archive` that bank `bank` uses and no other bank uses with a short silent wave
// (same channel count, DSP-ADPCM). Frees the sound memory those samples took inside their group; anything that
// plays them becomes silent. Returns the new archive; `count` gets the number of waves silenced.
Bytes silence_bank(ByteView data, std::uint32_t bank, std::uint32_t wave_archive, std::size_t* count = nullptr);

}  // namespace wiiumenu::bfsar
