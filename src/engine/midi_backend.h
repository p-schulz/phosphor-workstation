#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/messages.h"
#include "engine/midi_input.h"

// Device-independent MIDI input interface (PLAN.md §12: libremidi stays behind our own interface).
namespace phos::engine {

class MidiBackend {
public:
    virtual ~MidiBackend() = default;
    // Names of the MIDI inputs present right now (UI thread; cheap enough to poll).
    virtual std::vector<std::string> input_names() = 0;
    // Opens the input called `name` and feeds it into `queues` slot `slot`. The backend's
    // callback is that slot's only producer until close_input(slot) returns.
    virtual bool open_input(const std::string& name, int slot, MidiInputQueues& queues, std::string* error) = 0;
    virtual void close_input(int slot) = 0;
    virtual std::string api_name() const = 0;

    // MIDI out (M8): outputs in slots 0..kMaxMidiOutSlots-1. send() is called from the MIDI-out
    // thread; open / close from the UI thread (the backend serialises them).
    virtual std::vector<std::string> output_names() = 0;
    virtual bool open_output(const std::string& name, int slot, std::string* error) = 0;
    virtual void close_output(int slot) = 0;
    virtual bool send(int slot, const std::uint8_t* bytes, std::size_t size) = 0;
};

} // namespace phos::engine
