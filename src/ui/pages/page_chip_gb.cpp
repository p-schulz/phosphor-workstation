// Page C, Game Boy view: channel settings, the 32 x 16 wave grid and the macro lanes, both drawn
// with the light pen (PLAN.md §7.4).

#include <algorithm>
#include <cmath>

#include "ui/pages/chip_views.h"

namespace phos::ui::chip {

namespace {

enum Field : int {
    kChannel, kDuty, kQuant, kRaw, kEnvVol, kEnvDir, kEnvPace, kLength, kSweepPace, kSweepDir, kSweepShift,
    kNoise, kWaveLevel, kVibrato, kVibSpeed, kVibDelay, kPorta, kWave, kMacro, kMacroLen, kMacroLoop, kMacroRel,
    kStrict, kFieldCount
};

constexpr std::array<std::string_view, 3> kChannels = {"PULSE", "WAVE", "NOISE"};
constexpr std::array<std::string_view, 4> kDuties = {"12.5", "25", "50", "75"};
constexpr std::array<std::string_view, 4> kLevels = {"MUTE", "100", "50", "25"};
constexpr std::array<std::string_view, 5> kMacros = {"VOL", "DUTY", "ARP", "PITCH", "WAVE"};
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

// Value range per macro lane.
constexpr std::array<std::pair<int, int>, 5> kMacroRange = {{{0, 15}, {0, 3}, {-12, 12}, {-100, 100}, {0, 7}}};

constexpr gfx::Rect kGrid{px(1), py(10), 32 * 6, 16 * 6};    // wave: 32 x 16 cells of 6 px
constexpr gfx::Rect kLane{px(27), py(10), 64 * 4, 16 * 6};   // macro: up to 64 steps of 4 px

const FieldPos kPos[kFieldCount] = {
    {"CH", 1, 4, 3, 6},        {"DUTY", 11, 16, 3, 5},   {"QUANT", 22, 28, 3, 4},   {"RAW", 33, 37, 3, 4},
    {"ENV", 1, 5, 4, 3},       {"", 0, 9, 4, 5},          {"PACE", 15, 20, 4, 2},     {"LEN", 23, 27, 4, 4},
    {"SWEEP", 1, 7, 5, 2},     {"", 0, 10, 5, 5},         {"SHIFT", 16, 22, 5, 2},    {"NOISE", 25, 31, 5, 6},
    {"WAVE LVL", 38, 47, 5, 5}, {"VIB CT", 1, 8, 6, 4},   {"SPD", 13, 17, 6, 3},      {"DLY", 21, 25, 6, 3},
    {"PORTA", 29, 35, 6, 3},   {"WAVE", 1, 6, 8, 2},      {"MACRO", 27, 33, 8, 6},    {"LEN", 40, 44, 8, 3},
    {"LOOP", 48, 53, 8, 3},    {"REL", 57, 61, 8, 3},     {"STRICT GB", 44, 54, 2, 4},
};

int macro_len(const model::GbMacro& m) { return static_cast<int>(m.steps.size()); }

} // namespace

void GbView::update(const InputState& in, Ui& ui) {
    if (!in.pen_inside || !in.pen_down) {
        return;
    }
    const int x = static_cast<int>(in.pen_x);
    const int y = static_cast<int>(in.pen_y);
    model::Instrument inst = ui.app().workspace().doc().current_instrument();
    if (kGrid.contains(x, y)) {
        const int i = std::clamp((x - kGrid.x) / 6, 0, 31);
        const int v = std::clamp(15 - (y - kGrid.y) / 6, 0, 15);
        auto& cell = inst.gb.waves[static_cast<std::size_t>(wave_)][static_cast<std::size_t>(i)];
        if (cell != v) {
            cell = static_cast<std::uint8_t>(v);
            commit(ui, inst, "DRAW WAVE");
        }
    } else if (kLane.contains(x, y)) {
        auto& m = inst.gb.macros[static_cast<std::size_t>(macro_)];
        const int step = std::clamp((x - kLane.x) / 4, 0, 63);
        const auto [lo, hi] = kMacroRange[static_cast<std::size_t>(macro_)];
        const int v = std::clamp(static_cast<int>(std::lround(hi - static_cast<double>(y - kLane.y) / (kLane.h - 1) * (hi - lo))), lo, hi);
        if (step >= macro_len(m)) {
            m.steps.resize(static_cast<std::size_t>(step + 1), m.steps.empty() ? static_cast<std::int8_t>(v) : m.steps.back());
        }
        if (m.steps[static_cast<std::size_t>(step)] != v) {
            m.steps[static_cast<std::size_t>(step)] = static_cast<std::int8_t>(v);
            commit(ui, inst, "DRAW MACRO");
        }
    }
}

bool GbView::on_key(const InputEvent& ev, Ui& ui) {
    if (ev.type == InputEvent::Type::Text) {
        return ev.ch == '+' || ev.ch == '=' || ev.ch == '-' || ev.ch == '_'
                   ? (change(ui, (ev.ch == '+' || ev.ch == '=') ? 1 : -1, false), true)
                   : false;
    }
    const bool big = (ev.mods & kModShift) != 0;
    switch (ev.key) {
    case Key::Up:
        if (focus == 0) {
            ui.set_focus(Focus::Command);
        } else {
            --focus;
        }
        return true;
    case Key::Down:
    case Key::Tab:
        focus = std::min(focus + 1, kFieldCount - 1);
        return true;
    case Key::Left:
    case Key::Right:
    case Key::Enter:
        change(ui, ev.key == Key::Left ? -1 : 1, big);
        return true;
    default:
        return false;
    }
}

void GbView::on_zone(int zone, int wheel, Ui& ui) {
    focus = std::clamp(zone / 16, 0, kFieldCount - 1);
    change(ui, wheel != 0 ? wheel : 1, false);
}

void GbView::change(Ui& ui, int dir, bool big) {
    model::Workspace& ws = ui.app().workspace();
    model::Instrument inst = ws.doc().current_instrument();
    model::GbParams& g = inst.gb;
    auto cyc = [&](int v, int n) { return (v + dir + n) % n; };
    auto add = [&](int v, int lo, int hi, int step = 1) { return std::clamp(v + dir * step * (big ? 10 : 1), lo, hi); };
    auto& m = g.macros[static_cast<std::size_t>(macro_)];
    switch (focus) {
    case kChannel: {
        const auto ch = static_cast<model::GbChannel>(cyc(static_cast<int>(g.channel), 3));
        const auto keep = g.waves;
        g = model::default_gb(ch);
        g.waves = keep;
        break;
    }
    case kDuty: g.duty = cyc(g.duty, 4); break;
    case kQuant: g.quantize = !g.quantize; break;
    case kRaw: g.raw = !g.raw; break;
    case kEnvVol: g.env_volume = add(g.env_volume, 0, 15); break;
    case kEnvDir: g.env_up = !g.env_up; break;
    case kEnvPace: g.env_pace = add(g.env_pace, 0, 7); break;
    case kLength: g.length = add(g.length, 0, 64); break;
    case kSweepPace: g.sweep_pace = add(g.sweep_pace, 0, 7); break;
    case kSweepDir: g.sweep_down = !g.sweep_down; break;
    case kSweepShift: g.sweep_shift = add(g.sweep_shift, 0, 7); break;
    case kNoise: g.noise_short = !g.noise_short; break;
    case kWaveLevel: g.wave_level = cyc(g.wave_level, 4); break;
    case kVibrato: g.vibrato_cents = add(g.vibrato_cents, 0, 200, 5); break;
    case kVibSpeed: g.vibrato_speed = add(g.vibrato_speed, 1, 32); break;
    case kVibDelay: g.vibrato_delay = add(g.vibrato_delay, 0, 120); break;
    case kPorta: g.porta_ticks = add(g.porta_ticks, 0, 120); break;
    case kWave: wave_ = cyc(wave_, 8); return;
    case kMacro: macro_ = cyc(macro_, 5); return;
    case kMacroLen: {
        const int len = add(macro_len(m), 0, 64);
        m.steps.resize(static_cast<std::size_t>(len), m.steps.empty() ? std::int8_t{0} : m.steps.back());
        m.loop = std::min(m.loop, len - 1);
        m.release = std::min(m.release, len - 1);
        break;
    }
    case kMacroLoop: m.loop = std::clamp(m.loop + dir, -1, macro_len(m) - 1); break;
    case kMacroRel: m.release = std::clamp(m.release + dir, -1, macro_len(m) - 1); break;
    case kStrict: {
        model::Song s = ws.doc().song;
        s.strict_gb = !s.strict_gb;
        ws.set_song(s, "STRICT GB");
        return;
    }
    default: return;
    }
    commit(ui, inst, "GB " + std::string(kPos[focus].label.empty() ? "SETTING" : kPos[focus].label));
}

void GbView::draw(gfx::Canvas& c, const Ui& ui) const {
    const model::Document& doc = ui.app().workspace().doc();
    const model::GbParams& g = doc.current_instrument().gb;
    const bool fields = ui.focus() == Focus::Fields;
    auto f = [&](int id, const std::string& v) { field(c, ui, kPos[id], v, fields && focus == id, id * 16); };
    f(kChannel, std::string(kChannels[static_cast<std::size_t>(g.channel)]));
    f(kDuty, std::string(kDuties[static_cast<std::size_t>(g.duty)]));
    f(kQuant, std::string(kOffOn[g.quantize ? 1 : 0]));
    f(kRaw, std::string(kOffOn[g.raw ? 1 : 0]));
    f(kStrict, std::string(kOffOn[doc.song.strict_gb ? 1 : 0]));
    f(kEnvVol, num(g.env_volume));
    f(kEnvDir, g.env_up ? "UP" : "DOWN");
    f(kEnvPace, num(g.env_pace));
    f(kLength, g.length == 0 ? "OFF" : num(g.length));
    f(kSweepPace, num(g.sweep_pace));
    f(kSweepDir, g.sweep_down ? "DOWN" : "UP");
    f(kSweepShift, num(g.sweep_shift));
    f(kNoise, g.noise_short ? "7 BIT" : "15 BIT");
    f(kWaveLevel, std::string(kLevels[static_cast<std::size_t>(g.wave_level)]));
    f(kVibrato, num(g.vibrato_cents));
    f(kVibSpeed, num(g.vibrato_speed));
    f(kVibDelay, num(g.vibrato_delay));
    f(kPorta, num(g.porta_ticks));
    f(kWave, num(wave_ + 1));
    const auto& m = g.macros[static_cast<std::size_t>(macro_)];
    f(kMacro, std::string(kMacros[static_cast<std::size_t>(macro_)]));
    f(kMacroLen, num(macro_len(m)));
    f(kMacroLoop, m.loop < 0 ? "--" : num(m.loop + 1));
    f(kMacroRel, m.release < 0 ? "--" : num(m.release + 1));

    // Wave grid (32 x 16).
    c.rect(gfx::Rect{kGrid.x - 1, kGrid.y - 1, kGrid.w + 2, kGrid.h + 2}, gfx::kInkText);
    const auto& w = g.waves[static_cast<std::size_t>(wave_)];
    for (int i = 0; i < 32; ++i) {
        const int v = w[static_cast<std::size_t>(i)] & 15;
        c.fill_rect(gfx::Rect{kGrid.x + i * 6, kGrid.y + (15 - v) * 6, 5, (v + 1) * 6}, g.channel == model::GbChannel::Wave ? gfx::kInkText : gfx::kInkDim);
    }
    widgets::small_text(c, ui, kGrid.x, kGrid.y + kGrid.h + 4, "WAVE RAM: 32 X 4 BIT (DRAW)", gfx::kInkDim);

    // Macro lane.
    c.rect(gfx::Rect{kLane.x - 1, kLane.y - 1, kLane.w + 2, kLane.h + 2}, gfx::kInkText);
    const auto [lo, hi] = kMacroRange[static_cast<std::size_t>(macro_)];
    const int zero_y = kLane.y + static_cast<int>(static_cast<double>(hi) / (hi - lo) * (kLane.h - 1));
    if (lo < 0) {
        c.hline(kLane.x, kLane.x + kLane.w - 1, zero_y, gfx::kInkGrid);
    }
    for (int s = 0; s < macro_len(m); ++s) {
        const int v = m.steps[static_cast<std::size_t>(s)];
        const int y = kLane.y + static_cast<int>(static_cast<double>(hi - v) / (hi - lo) * (kLane.h - 1));
        const int top = lo < 0 ? std::min(y, zero_y) : y;
        const int bottom = lo < 0 ? std::max(y, zero_y) : kLane.y + kLane.h - 1;
        const bool in_loop = m.loop >= 0 && s >= m.loop && (m.release < 0 || s < m.release);
        c.fill_rect(gfx::Rect{kLane.x + s * 4, top, 3, std::max(1, bottom - top + 1)}, in_loop ? gfx::kInkBright : gfx::kInkText);
    }
    if (m.release >= 0) {
        c.vline(kLane.x + m.release * 4 - 1, kLane.y, kLane.y + kLane.h - 1, gfx::kInkWarn);
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s %d..%d AT 60 HZ. LOOP BRIGHT",
                  std::string(kMacros[static_cast<std::size_t>(macro_)]).c_str(), lo, hi);
    widgets::small_text(c, ui, kLane.x, kLane.y + kLane.h + 4, buf, gfx::kInkDim);
    widgets::small_text(c, ui, px(1), py(21) + 2,
                        "PULSE: DUTY, SWEEP. WAVE: WAVE RAM, LEVEL. NOISE: LFSR WIDTH, NOTE = CLOCK.",
                        gfx::kInkDim);
    widgets::small_text(c, ui, px(1), py(22) + 2,
                        "QUANT: 11-BIT PERIOD PITCH (AUTHENTIC). RAW: NAIVE STEPS (ALIASING).", gfx::kInkDim);
    widgets::small_text(c, ui, px(1), py(23) + 2,
                        "STRICT GB: 2 PULSE + WAVE + NOISE, ONE NOTE EACH. RED LINE = RELEASE.", gfx::kInkDim);
}

} // namespace phos::ui::chip
