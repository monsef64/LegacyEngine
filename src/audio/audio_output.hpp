#pragma once
#include <cstddef>
#include <memory>
#include <vector>

// Owns a miniaudio playback device and a bounded stereo float PCM queue.
// The audio callback only touches this queue, never ECS state or asset files.
class AudioOutput {
public:
    // Null device is explicitly opt-in for headless regression tests only.
    explicit AudioOutput(bool nullDevice=false);
    ~AudioOutput();
    AudioOutput(const AudioOutput&)=delete;
    AudioOutput& operator=(const AudioOutput&)=delete;
    void open();
    void clear();
    std::size_t queuedFrames() const;
    void submit(const std::vector<float>& samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
