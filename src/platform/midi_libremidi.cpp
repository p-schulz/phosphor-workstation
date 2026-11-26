#include "platform/midi_libremidi.h"

#include <array>
#include <mutex>
#include <optional>
#include <span>

#include <libremidi/libremidi.hpp>

namespace phos::platform {

namespace {

template <typename Port> std::string port_label(const Port& p) {
    return p.display_name.empty() ? p.port_name : p.display_name;
}

} // namespace

struct LibremidiBackend::Impl {
    libremidi::observer observer{libremidi::observer_configuration{
        .track_hardware = true, .track_virtual = true, .track_any = true, .notify_in_constructor = false}};
    std::array<std::unique_ptr<libremidi::midi_in>, engine::kMaxMidiSlots> inputs;
    std::mutex out_mutex; // outputs: opened / closed on the UI thread, used by the MIDI-out thread
    std::array<std::unique_ptr<libremidi::midi_out>, engine::kMaxMidiOutSlots> outputs;

    std::optional<libremidi::input_port> find(const std::string& name) {
        for (auto& p : observer.get_input_ports()) {
            if (port_label(p) == name) {
                return p;
            }
        }
        return std::nullopt;
    }
};

LibremidiBackend::LibremidiBackend() : impl_(std::make_unique<Impl>()) {}

LibremidiBackend::~LibremidiBackend() {
    for (int s = 0; s < engine::kMaxMidiSlots; ++s) {
        close_input(s);
    }
    for (int s = 0; s < engine::kMaxMidiOutSlots; ++s) {
        close_output(s);
    }
}

std::vector<std::string> LibremidiBackend::output_names() {
    std::vector<std::string> out;
    for (const auto& p : impl_->observer.get_output_ports()) {
        out.push_back(port_label(p));
    }
    return out;
}

bool LibremidiBackend::open_output(const std::string& name, int slot, std::string* error) {
    close_output(slot);
    std::optional<libremidi::output_port> port;
    for (auto& p : impl_->observer.get_output_ports()) {
        if (port_label(p) == name) {
            port = p;
        }
    }
    if (!port) {
        if (error) {
            *error = "not connected";
        }
        return false;
    }
    auto out = std::make_unique<libremidi::midi_out>();
    if (auto err = out->open_port(*port, "PHOSPHOR out"); err != stdx::error{}) {
        if (error) {
            *error = "cannot open";
        }
        return false;
    }
    std::lock_guard lock(impl_->out_mutex);
    impl_->outputs[static_cast<std::size_t>(slot)] = std::move(out);
    return true;
}

bool LibremidiBackend::open_virtual_output(const std::string& name, int slot, std::string* error) {
    close_output(slot);
    auto out = std::make_unique<libremidi::midi_out>();
    if (auto err = out->open_virtual_port(name); err != stdx::error{}) {
        if (error) {
            *error = "virtual ports are not supported by " + api_name();
        }
        return false;
    }
    std::lock_guard lock(impl_->out_mutex);
    impl_->outputs[static_cast<std::size_t>(slot)] = std::move(out);
    return true;
}

void LibremidiBackend::close_output(int slot) {
    std::unique_ptr<libremidi::midi_out> old;
    {
        std::lock_guard lock(impl_->out_mutex);
        old = std::move(impl_->outputs[static_cast<std::size_t>(slot)]);
    }
    if (old) {
        old->close_port();
    }
}

bool LibremidiBackend::send(int slot, const std::uint8_t* bytes, std::size_t size) {
    std::lock_guard lock(impl_->out_mutex);
    auto& out = impl_->outputs[static_cast<std::size_t>(slot)];
    return out && out->send_message(bytes, size) == stdx::error{};
}

std::vector<std::string> LibremidiBackend::input_names() {
    std::vector<std::string> out;
    for (const auto& p : impl_->observer.get_input_ports()) {
        out.push_back(port_label(p));
    }
    return out;
}

bool LibremidiBackend::open_input(const std::string& name, int slot, engine::MidiInputQueues& queues,
                                  std::string* error) {
    close_input(slot);
    const auto port = impl_->find(name);
    if (!port) {
        if (error) {
            *error = "not connected";
        }
        return false;
    }
    queues.reset_parser(slot);
    libremidi::input_configuration conf;
    // The MIDI-in callback only timestamps and pushes (CLAUDE.md): raw bytes, no allocation.
    conf.on_raw_data = [&queues, slot](std::span<const std::uint8_t> bytes, libremidi::timestamp ts) {
        queues.push_bytes(slot, bytes.data(), bytes.size(), ts);
    };
    conf.timestamps = libremidi::timestamp_mode::SystemMonotonic;
    auto in = std::make_unique<libremidi::midi_in>(conf);
    if (auto err = in->open_port(*port, "PHOSPHOR in"); err != stdx::error{}) {
        if (error) {
            *error = "cannot open";
        }
        return false;
    }
    impl_->inputs[static_cast<std::size_t>(slot)] = std::move(in);
    return true;
}

void LibremidiBackend::close_input(int slot) {
    auto& in = impl_->inputs[static_cast<std::size_t>(slot)];
    if (in) {
        in->close_port();
        in.reset(); // no callbacks for this slot after this returns
    }
}

std::string LibremidiBackend::api_name() const {
    return std::string(libremidi::get_api_display_name(impl_->observer.get_current_api()));
}

} // namespace phos::platform
