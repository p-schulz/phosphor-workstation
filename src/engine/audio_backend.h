#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Device-independent audio I/O interface (PLAN.md §5, §6). The miniaudio implementation lives in
// src/platform; phosphor_core and the tests only see this header.
namespace phos::engine {

struct AudioDeviceInfo {
    std::string name;
    bool is_default = false;
};

struct AudioConfig {
    bool enabled = true;
    std::string device;              // empty = system default output
    std::uint32_t sample_rate = 48000;
    std::uint32_t buffer_frames = 128; // requested period, 32..1024
    bool exclusive = false;          // WASAPI exclusive; ignored where unsupported
    friend bool operator==(const AudioConfig&, const AudioConfig&) = default;
};

inline constexpr std::uint32_t kBufferChoices[] = {32, 64, 128, 256, 512, 1024};
inline constexpr std::uint32_t kRateChoices[] = {44100, 48000, 88200, 96000};

struct AudioStatus {
    bool running = false;
    std::string device;              // name of the opened device
    std::uint32_t sample_rate = 0;   // actual rate
    std::uint32_t buffer_frames = 0; // actual period
    std::uint32_t periods = 0;
    std::uint32_t channels = 0;
    bool exclusive = false;
    std::string error;               // last open error, or why the device stopped
};

// Implemented by the engine. prepare() runs on the opening thread while the device is stopped;
// process() runs on the audio thread and must obey the realtime contract (PLAN.md §6.2).
class AudioCallback {
public:
    virtual ~AudioCallback() = default;
    virtual void prepare(std::uint32_t sample_rate, std::uint32_t channels) = 0;
    // `out` is interleaved float, frames * channels. `host_ns` is the monotonic clock at callback
    // entry (std::chrono::steady_clock), or 0 for offline rendering.
    virtual void process(float* out, std::uint32_t frames, std::uint32_t channels, std::int64_t host_ns) noexcept = 0;
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual std::vector<AudioDeviceInfo> list_devices() = 0;
    // Closes any open device, then opens and starts a new one. On failure status().error says why.
    virtual bool open(const AudioConfig& config, AudioCallback& callback) = 0;
    virtual void close() = 0;
    virtual AudioStatus status() const = 0;
    // True once after the device stopped without close() (unplugged, taken over by another app).
    virtual bool take_stopped_unexpectedly() = 0;
};

} // namespace phos::engine
