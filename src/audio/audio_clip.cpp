#include "audio/audio_clip.hpp"
#include <array>
#include <miniaudio.h>

std::vector<float> decodeAudioClip(const AssetManager& assets, const std::string& path) {
    const auto file = assets.resolve(path);
    if (!audioAsset(file) || std::filesystem::file_size(file) > 32 * 1024 * 1024) {
        throw std::runtime_error("Audio requires WAV or MP3 under 32 MiB");
    }
    auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
    ma_decoder decoder{};
#ifdef _WIN32
    const auto result = ma_decoder_init_file_w(file.c_str(), &config, &decoder);
#else
    const auto result = ma_decoder_init_file(file.c_str(), &config, &decoder);
#endif
    if (result != MA_SUCCESS) {
        throw std::runtime_error(std::string("Cannot decode audio: ") +
                                 ma_result_description(result));
    }
    struct Close {
        ma_decoder* decoder;
        ~Close() {
            ma_decoder_uninit(decoder);
        }
    } close{&decoder};
    constexpr std::size_t maxSamples = 128 * 1024 * 1024 / sizeof(float);
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS &&
        length > maxSamples / 2) {
        throw std::runtime_error("Decoded audio exceeds 128 MiB");
    }
    std::vector<float> samples;
    std::array<float, 4096 * 2> block{};
    for (;;) {
        ma_uint64 frames = 0;
        const auto read = ma_decoder_read_pcm_frames(&decoder, block.data(), 4096, &frames);
        if (read != MA_SUCCESS && read != MA_AT_END) {
            throw std::runtime_error(std::string("Cannot read audio: ") +
                                     ma_result_description(read));
        }
        if (frames > 4096 || samples.size() + frames * 2 > maxSamples) {
            throw std::runtime_error("Decoded audio exceeds 128 MiB");
        }
        samples.insert(
            samples.end(), block.begin(), block.begin() + static_cast<std::size_t>(frames) * 2);
        if (read == MA_AT_END || frames == 0) {
            break;
        }
    }
    if (samples.empty()) {
        throw std::runtime_error("Empty audio clip");
    }
    return samples;
}
