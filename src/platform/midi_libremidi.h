#pragma once

#include <memory>

#include "engine/midi_backend.h"

namespace phos::platform {

// MidiBackend on libremidi (CoreMIDI on macOS, WinMM on Windows). Inputs deliver raw bytes with
// monotonic nanosecond timestamps (libremidi::timestamp_mode::SystemMonotonic, the same clock as
// std::chrono::steady_clock and the audio callback stamps).
class LibremidiBackend final : public engine::MidiBackend {
public:
    LibremidiBackend();
    ~LibremidiBackend() override;
    LibremidiBackend(const LibremidiBackend&) = delete;
    LibremidiBackend& operator=(const LibremidiBackend&) = delete;

    std::vector<std::string> input_names() override;
    bool open_input(const std::string& name, int slot, engine::MidiInputQueues& queues, std::string* error) override;
    void close_input(int slot) override;
    std::string api_name() const override;
    std::vector<std::string> output_names() override;
    bool open_output(const std::string& name, int slot, std::string* error) override;
    void close_output(int slot) override;
    bool send(int slot, const std::uint8_t* bytes, std::size_t size) override;
    // A virtual output other programs (and this one) can read: CoreMIDI only (--midi-loopback).
    bool open_virtual_output(const std::string& name, int slot, std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace phos::platform
