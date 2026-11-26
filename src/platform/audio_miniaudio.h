#pragma once

#include <memory>

#include "engine/audio_backend.h"

namespace phos::platform {

// AudioBackend on miniaudio: CoreAudio on macOS, WASAPI (shared or exclusive) on Windows.
// Float32 stereo output, low-latency profile, the backend's native callback size (no extra
// fixed-size buffering inside miniaudio).
class MiniaudioBackend final : public engine::AudioBackend {
public:
    MiniaudioBackend();
    ~MiniaudioBackend() override;
    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

    std::vector<engine::AudioDeviceInfo> list_devices() override;
    bool open(const engine::AudioConfig& config, engine::AudioCallback& callback) override;
    void close() override;
    engine::AudioStatus status() const override;
    bool take_stopped_unexpectedly() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace phos::platform
