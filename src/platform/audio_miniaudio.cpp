#include "platform/audio_miniaudio.h"

#include <atomic>
#include <chrono>
#include <cstring>

#include <miniaudio.h>

namespace phos::platform {

struct MiniaudioBackend::Impl {
    ma_context context{};
    bool context_ok = false;
    ma_device device{};
    bool device_open = false;
    engine::AudioCallback* callback = nullptr;
    engine::AudioStatus status;
    std::atomic<bool> closing{false};
    std::atomic<bool> stopped_unexpectedly{false};

    static void data(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
        self->callback->process(static_cast<float*>(out), frames, dev->playback.channels, now);
    }

    static void notification(const ma_device_notification* n) {
        auto* self = static_cast<Impl*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && !self->closing.load()) {
            self->stopped_unexpectedly.store(true);
        }
    }

    // Copies the playback device ID whose name matches; false = use the default device.
    bool find_device(const std::string& name, ma_device_id* id) {
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (name.empty() || ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
            return false;
        }
        for (ma_uint32 i = 0; i < count; ++i) {
            if (name == infos[i].name) {
                *id = infos[i].id;
                return true;
            }
        }
        return false;
    }
};

MiniaudioBackend::MiniaudioBackend() : impl_(std::make_unique<Impl>()) {
    ma_context_config cfg = ma_context_config_init();
    impl_->context_ok = ma_context_init(nullptr, 0, &cfg, &impl_->context) == MA_SUCCESS;
    if (!impl_->context_ok) {
        impl_->status.error = "NO AUDIO SYSTEM";
    }
}

MiniaudioBackend::~MiniaudioBackend() {
    close();
    if (impl_->context_ok) {
        ma_context_uninit(&impl_->context);
    }
}

std::vector<engine::AudioDeviceInfo> MiniaudioBackend::list_devices() {
    std::vector<engine::AudioDeviceInfo> out;
    if (!impl_->context_ok) {
        return out;
    }
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&impl_->context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        return out;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        out.push_back({infos[i].name, infos[i].isDefault != 0});
    }
    return out;
}

bool MiniaudioBackend::open(const engine::AudioConfig& config, engine::AudioCallback& callback) {
    close();
    Impl& m = *impl_;
    m.status = {};
    if (!m.context_ok) {
        m.status.error = "NO AUDIO SYSTEM";
        return false;
    }
    if (!config.enabled) {
        m.status.error = "AUDIO OFF";
        return false;
    }

    ma_device_id id{};
    const bool named = m.find_device(config.device, &id);

    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.pDeviceID = named ? &id : nullptr;
    c.playback.format = ma_format_f32;
    c.playback.channels = 2;
    c.playback.shareMode = config.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    c.sampleRate = config.sample_rate;
    c.periodSizeInFrames = config.buffer_frames;
    c.performanceProfile = ma_performance_profile_low_latency;
    c.noPreSilencedOutputBuffer = MA_TRUE; // the engine writes every frame
    c.noClip = MA_TRUE;                    // the master limiter already keeps the output in range
    c.noFixedSizedCallback = MA_TRUE;      // no extra buffering; the engine accepts any block size
    c.coreaudio.allowNominalSampleRateChange = MA_TRUE; // run the device at the requested rate
    c.dataCallback = &Impl::data;
    c.notificationCallback = &Impl::notification;
    c.pUserData = &m;

    m.callback = &callback;
    if (ma_device_init(&m.context, &c, &m.device) != MA_SUCCESS) {
        m.status.error = config.exclusive ? "CANNOT OPEN DEVICE (EXCLUSIVE)" : "CANNOT OPEN DEVICE";
        return false;
    }
    m.device_open = true;
    m.closing.store(false);
    m.stopped_unexpectedly.store(false);

    callback.prepare(m.device.sampleRate, m.device.playback.channels);
    if (ma_device_start(&m.device) != MA_SUCCESS) {
        m.status.error = "CANNOT START DEVICE";
        close();
        return false;
    }
    m.status.running = true;
    m.status.device = m.device.playback.name;
    m.status.sample_rate = m.device.sampleRate;
    m.status.buffer_frames = m.device.playback.internalPeriodSizeInFrames;
    m.status.periods = m.device.playback.internalPeriods;
    m.status.channels = m.device.playback.channels;
    m.status.exclusive = m.device.playback.shareMode == ma_share_mode_exclusive;
    return true;
}

void MiniaudioBackend::close() {
    Impl& m = *impl_;
    if (!m.device_open) {
        return;
    }
    m.closing.store(true);
    ma_device_uninit(&m.device); // stops the device and joins the audio thread
    m.device_open = false;
    m.status.running = false;
}

engine::AudioStatus MiniaudioBackend::status() const { return impl_->status; }

bool MiniaudioBackend::take_stopped_unexpectedly() {
    if (!impl_->stopped_unexpectedly.exchange(false)) {
        return false;
    }
    impl_->status.running = false;
    impl_->status.error = "DEVICE STOPPED";
    return true;
}

} // namespace phos::platform
