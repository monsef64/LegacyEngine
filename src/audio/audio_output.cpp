#include "audio/audio_output.hpp"
#include <algorithm>
#include <array>
#include <miniaudio.h>
#include <mutex>
#include <stdexcept>
#include <string>

struct AudioOutput::Impl {
    static constexpr std::size_t capacity = 48000 * 2;
    std::array<float, capacity> samples{};
    mutable std::mutex mutex;
    std::size_t head = 0, size = 0;
    bool nullDevice = false, contextReady = false, deviceReady = false;
    ma_context context{};
    ma_device device{};
    explicit Impl(bool useNull) : nullDevice(useNull) {}
    ~Impl() {
        if (deviceReady) {
            ma_device_uninit(&device);
        }
        if (contextReady) {
            ma_context_uninit(&context);
        }
    }
    static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto& self = *static_cast<Impl*>(device->pUserData);
        auto* target = static_cast<float*>(output);
        const std::size_t requested = std::size_t(frames) * 2;
        std::fill(target, target + requested, 0.0f);
        std::lock_guard<std::mutex> lock(self.mutex);
        const auto count = std::min(requested, self.size);
        for (std::size_t i = 0; i < count; ++i) {
            target[i] = self.samples[(self.head + i) % capacity];
        }
        self.head = (self.head + count) % capacity;
        self.size -= count;
    }
};
AudioOutput::AudioOutput(bool nullDevice) : impl_(std::make_unique<Impl>(nullDevice)) {}
AudioOutput::~AudioOutput() = default;
void AudioOutput::open() {
    auto& self = *impl_;
    if (self.deviceReady) {
        return;
    }
    auto fail = [](const char* operation, ma_result result) {
        throw std::runtime_error(std::string(operation) + ": " + ma_result_description(result));
    };
    if (!self.contextReady) {
        const ma_backend backend = ma_backend_null;
        auto result = ma_context_init(
            self.nullDevice ? &backend : nullptr, self.nullDevice ? 1 : 0, nullptr, &self.context);
        if (result != MA_SUCCESS) {
            fail("Cannot initialize miniaudio", result);
        }
        self.contextReady = true;
    }
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.dataCallback = Impl::callback;
    config.pUserData = &self;
    auto result = ma_device_init(&self.context, &config, &self.device);
    if (result != MA_SUCCESS) {
        fail("Cannot open audio device", result);
    }
    result = ma_device_start(&self.device);
    if (result != MA_SUCCESS) {
        ma_device_uninit(&self.device);
        fail("Cannot start audio device", result);
    }
    self.deviceReady = true;
}
void AudioOutput::clear() {
    auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.mutex);
    self.head = self.size = 0;
}
std::size_t AudioOutput::queuedFrames() const {
    auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.mutex);
    return self.size / 2;
}
void AudioOutput::submit(const std::vector<float>& samples) {
    auto& self = *impl_;
    std::lock_guard<std::mutex> lock(self.mutex);
    if (samples.size() % 2 || samples.size() > Impl::capacity - self.size) {
        throw std::runtime_error("Audio output queue exceeds stereo PCM capacity");
    }
    for (std::size_t i = 0; i < samples.size(); ++i) {
        self.samples[(self.head + self.size + i) % Impl::capacity] = samples[i];
    }
    self.size += samples.size();
}
