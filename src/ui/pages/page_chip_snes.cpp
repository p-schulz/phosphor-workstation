// Page C, SNES view: source rate / A-B / ADSR or GAIN with its curve, volumes and flags; the
// song-wide echo with an 8-fader FIR editor; the ARAM budget bar (PLAN.md §7.4).

#include <algorithm>
#include <cmath>

#include "io/snes.h"
#include "ui/pages/chip_views.h"

namespace phos::ui::chip {

namespace {

enum Field : int {
    kRate, kAlign, kAB, kRoot, kEnvMode, kAR, kDR, kSL, kSR, kGainMode, kGain, kVolL, kVolR, kNoise, kNoiseRate,
    kPmod, kEcho, kEdl, kEfb, kEvolL, kEvolR, kPreset, kStrict, kFieldCount
};
constexpr std::array<std::string_view, 5> kGainModes = {"DIRECT", "LIN-", "EXP-", "LIN+", "BENT+"};
constexpr std::array<std::string_view, 4> kPresets = {"FLAT", "LOWPASS", "SOFT", "BRIGHT"};
constexpr std::array<std::string_view, 2> kOffOn = {"OFF", "ON"};

const FieldPos kPos[kFieldCount] = {
    {"RATE", 1, 6, 3, 6},     {"ALIGN", 13, 19, 3, 4},  {"A/B", 24, 28, 3, 5},     {"ROOT", 34, 39, 3, 4},
    {"ENV", 1, 5, 4, 5},      {"AR", 11, 14, 4, 3},     {"DR", 18, 21, 4, 2},      {"SL", 24, 27, 4, 2},
    {"SR", 30, 33, 4, 3},     {"GAIN", 37, 42, 4, 6},   {"", 0, 49, 4, 4},         {"VOL L", 1, 7, 5, 4},
    {"R", 12, 14, 5, 4},      {"NOISE", 19, 25, 5, 4},  {"CLK", 30, 34, 5, 3},     {"PMOD", 38, 43, 5, 4},
    {"ECHO", 48, 53, 5, 4},   {"EDL", 34, 38, 15, 3},   {"EFB", 34, 38, 16, 4},    {"EVOL L", 34, 41, 17, 4},
    {"R", 46, 48, 17, 4},     {"PRESET", 34, 41, 18, 8}, {"STRICT SNES", 44, 56, 3, 4},
};
constexpr gfx::Rect kEnvGraph{px(1), py(7), 230, py(13) - py(7)};
constexpr gfx::Rect kFir{px(1), py(15), 8 * 26, py(21) - py(15)};

// Envelope level (0..0x7FF) over `samples` at 32 kHz with the S-DSP rules (fullsnes
// "ADSR/Gain Envelope"), key released at `release_at`.
std::array<int, 230> simulate(const model::SnesParams& p, int samples, int release_at) {
    std::array<int, 230> out{};
    int env = 0;
    int stage = 0; // 0 attack, 1 decay, 2 sustain, 3 release
    int counter = 0;
    auto due = [&](int rate) {
        const int period = model::kSnesRates[static_cast<std::size_t>(std::clamp(rate, 0, 31))];
        if (period == 0 || --counter > 0) {
            return false;
        }
        counter = period;
        return true;
    };
    for (int t = 0; t < samples; ++t) {
        if (t == release_at) {
            stage = 3;
        }
        if (stage == 3) {
            env = std::max(0, env - 8);
        } else if (p.adsr) {
            if (stage == 0 && due(p.attack * 2 + 1)) {
                env += p.attack == 15 ? 1024 : 32;
            } else if (stage == 1 && due(p.decay * 2 + 16)) {
                env -= ((env - 1) >> 8) + 1;
            } else if (stage == 2 && due(p.sustain_rate)) {
                env -= ((env - 1) >> 8) + 1;
            }
            if (stage == 0 && env >= 0x7E0) {
                stage = 1;
            }
            if (stage == 1 && env <= (p.sustain_level + 1) * 0x100) {
                stage = 2;
            }
        } else if (p.gain_mode == model::SnesGainMode::Direct) {
            env = p.gain * 16;
        } else if (due(p.gain & 31)) {
            env += p.gain_mode == model::SnesGainMode::LinearIncrease ? 32
                   : p.gain_mode == model::SnesGainMode::BentIncrease ? (env < 0x600 ? 32 : 8)
                   : p.gain_mode == model::SnesGainMode::LinearDecrease ? -32
                                                                         : -(((env - 1) >> 8) + 1);
        }
        env = std::clamp(env, 0, 0x7FF);
        out[static_cast<std::size_t>(t * 230 / samples)] = env;
    }
    return out;
}

} // namespace

void SnesView::update(const InputState& in, Ui& ui) {
    if (!in.pen_inside || !in.pen_down) {
        return;
    }
    const int x = static_cast<int>(in.pen_x);
    const int y = static_cast<int>(in.pen_y);
    if (!kFir.contains(x, y)) {
        return;
    }
    // FIR fader: -128 at the bottom, +127 at the top.
    model::Workspace& ws = ui.app().workspace();
    model::Song s = ws.doc().song;
    const int k = std::clamp((x - kFir.x) / 26, 0, 7);
    const int v = std::clamp(static_cast<int>(std::lround(127.0 - static_cast<double>(y - kFir.y) / (kFir.h - 1) * 255.0)), -128, 127);
    if (s.snes_echo.fir[static_cast<std::size_t>(k)] != v) {
        s.snes_echo.fir[static_cast<std::size_t>(k)] = v;
        ws.set_song(s, "ECHO FIR", ui.app().time_s());
    }
}

bool SnesView::on_key(const InputEvent& ev, Ui& ui) {
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

void SnesView::on_zone(int zone, int wheel, Ui& ui) {
    focus = std::clamp(zone / 16, 0, kFieldCount - 1);
    change(ui, wheel != 0 ? wheel : 1, false);
}

void SnesView::reencode(Ui& ui, model::Instrument inst, const std::string& label) {
    if (!inst.sample) {
        ui.message("NO ORIGINAL SAMPLE TO RE-ENCODE", true);
        return;
    }
    inst.brr = io::encode_for_snes(*inst.sample, inst.sampler, inst.snes);
    commit(ui, inst, label);
}

void SnesView::change(Ui& ui, int dir, bool big) {
    model::Workspace& ws = ui.app().workspace();
    model::Instrument inst = ws.doc().current_instrument();
    model::SnesParams& p = inst.snes;
    model::Song song = ws.doc().song;
    model::SnesEcho& e = song.snes_echo;
    auto add = [&](int v, int lo, int hi, int step = 1) { return std::clamp(v + dir * step * (big ? 10 : 1), lo, hi); };
    bool song_edit = false;
    switch (focus) {
    case kRate:
        p.source_rate = add(p.source_rate, 8000, 32000, 1000);
        reencode(ui, inst, "SNES RATE");
        return;
    case kAlign:
        p.auto_align = !p.auto_align;
        reencode(ui, inst, "SNES ALIGN");
        return;
    case kAB: p.preview_original = !p.preview_original; break;
    case kRoot: p.root_note = add(p.root_note, 0, 127); break;
    case kEnvMode: p.adsr = !p.adsr; break;
    case kAR: p.attack = add(p.attack, 0, 15); break;
    case kDR: p.decay = add(p.decay, 0, 7); break;
    case kSL: p.sustain_level = add(p.sustain_level, 0, 7); break;
    case kSR: p.sustain_rate = add(p.sustain_rate, 0, 31); break;
    case kGainMode: p.gain_mode = static_cast<model::SnesGainMode>((static_cast<int>(p.gain_mode) + dir + 5) % 5); break;
    case kGain: p.gain = add(p.gain, 0, 127); break;
    case kVolL: p.vol_l = add(p.vol_l, -128, 127); break;
    case kVolR: p.vol_r = add(p.vol_r, -128, 127); break;
    case kNoise: p.noise = !p.noise; break;
    case kPmod: p.pitch_mod = !p.pitch_mod; break;
    case kEcho: p.echo = !p.echo; break;
    case kNoiseRate: e.noise_rate = add(e.noise_rate, 0, 31); song_edit = true; break;
    case kEdl: e.delay = add(e.delay, 0, 15); song_edit = true; break;
    case kEfb: e.feedback = add(e.feedback, -128, 127); song_edit = true; break;
    case kEvolL: e.vol_l = add(e.vol_l, -128, 127); song_edit = true; break;
    case kEvolR: e.vol_r = add(e.vol_r, -128, 127); song_edit = true; break;
    case kPreset: {
        const auto it = std::find(model::kFirPresets.begin(), model::kFirPresets.end(), e.fir);
        const int cur = it == model::kFirPresets.end() ? -1 : static_cast<int>(it - model::kFirPresets.begin());
        e.fir = model::kFirPresets[static_cast<std::size_t>((cur + dir + 4) % 4)];
        song_edit = true;
        break;
    }
    case kStrict: song.strict_snes = !song.strict_snes; song_edit = true; break;
    default: return;
    }
    if (song_edit) {
        ws.set_song(song, "SNES ECHO", ui.app().time_s());
    } else {
        commit(ui, inst, "SNES " + std::string(kPos[focus].label.empty() ? "GAIN" : kPos[focus].label));
    }
}

void SnesView::draw(gfx::Canvas& c, const Ui& ui) const {
    const model::Document& doc = ui.app().workspace().doc();
    const model::Instrument& inst = doc.current_instrument();
    const model::SnesParams& p = inst.snes;
    const model::SnesEcho& e = doc.song.snes_echo;
    const bool fields = ui.focus() == Focus::Fields;
    auto f = [&](int id, const std::string& v) { field(c, ui, kPos[id], v, fields && focus == id, id * 16); };
    f(kRate, num(p.source_rate));
    f(kAlign, std::string(kOffOn[p.auto_align ? 1 : 0]));
    f(kAB, p.preview_original ? "ORIG" : "BRR");
    f(kRoot, num(p.root_note));
    f(kStrict, std::string(kOffOn[doc.song.strict_snes ? 1 : 0]));
    f(kEnvMode, p.adsr ? "ADSR" : "GAIN");
    f(kAR, num(p.attack));
    f(kDR, num(p.decay));
    f(kSL, num(p.sustain_level));
    f(kSR, num(p.sustain_rate));
    f(kGainMode, std::string(kGainModes[static_cast<std::size_t>(p.gain_mode)]));
    f(kGain, num(p.gain));
    f(kVolL, num(p.vol_l));
    f(kVolR, num(p.vol_r));
    f(kNoise, std::string(kOffOn[p.noise ? 1 : 0]));
    f(kNoiseRate, num(e.noise_rate));
    f(kPmod, std::string(kOffOn[p.pitch_mod ? 1 : 0]));
    f(kEcho, std::string(kOffOn[p.echo ? 1 : 0]));
    f(kEdl, num(e.delay));
    f(kEfb, num(e.feedback));
    f(kEvolL, num(e.vol_l));
    f(kEvolR, num(e.vol_r));
    const auto preset = std::find(model::kFirPresets.begin(), model::kFirPresets.end(), e.fir);
    f(kPreset, preset == model::kFirPresets.end() ? "CUSTOM" : std::string(kPresets[static_cast<std::size_t>(preset - model::kFirPresets.begin())]));

    // Envelope curve: 1 s held, then released.
    c.rect(gfx::Rect{kEnvGraph.x - 1, kEnvGraph.y - 1, kEnvGraph.w + 2, kEnvGraph.h + 2}, gfx::kInkText);
    const auto env = simulate(p, 48000, 32000);
    for (int x = 1; x < kEnvGraph.w; ++x) {
        const int y0 = kEnvGraph.y + kEnvGraph.h - 3 - env[static_cast<std::size_t>(x - 1)] * (kEnvGraph.h - 14) / 0x7FF;
        const int y1 = kEnvGraph.y + kEnvGraph.h - 3 - env[static_cast<std::size_t>(x)] * (kEnvGraph.h - 14) / 0x7FF;
        c.line(kEnvGraph.x + x - 1, y0, kEnvGraph.x + x, y1, gfx::kInkBright);
    }
    c.dotted_line(kEnvGraph.x + 153, kEnvGraph.y, kEnvGraph.x + 153, kEnvGraph.y + kEnvGraph.h - 1, 2, gfx::kInkGrid);
    widgets::small_text(c, ui, kEnvGraph.x + 2, kEnvGraph.y + 2, p.adsr ? "ADSR, KEY UP AT 1 S" : "GAIN, KEY UP AT 1 S", gfx::kInkDim);
    char buf[96];
    if (inst.brr) {
        std::snprintf(buf, sizeof buf, "BRR %zu BYTES @ %d HZ%s", inst.brr->data.size(), inst.brr->source_rate,
                      inst.brr->loop_block >= 0 ? " LOOP" : "");
    } else {
        std::snprintf(buf, sizeof buf, "NO BRR: KIND SNES CONVERTS THE IMPORTED SAMPLE");
    }
    widgets::small_text(c, ui, px(31), py(7) + 2, buf, inst.brr ? gfx::kInkText : gfx::kInkWarn);
    widgets::small_text(c, ui, px(31), py(8) + 2, "A/B ORIG PLAYS THE SAMPLE BEFORE BRR.", gfx::kInkDim);
    widgets::small_text(c, ui, px(31), py(9) + 2, "PMOD: PITCH FROM THE PREVIOUS SNES VOICE.", gfx::kInkDim);
    widgets::small_text(c, ui, px(31), py(10) + 2, "NOISE CLK AND ECHO ARE SONG-WIDE (ONE DSP).", gfx::kInkDim);

    // Echo FIR faders.
    widgets::text(c, ui, 1, 14, "ECHO FIR (DRAW)", gfx::kInkDim);
    std::snprintf(buf, sizeof buf, "DELAY %d MS", e.delay * 16);
    widgets::small_text(c, ui, px(43), py(15) + 3, buf, gfx::kInkDim);
    c.rect(gfx::Rect{kFir.x - 1, kFir.y - 1, kFir.w + 2, kFir.h + 2}, gfx::kInkText);
    const int mid = kFir.y + static_cast<int>(127.0 / 255.0 * (kFir.h - 1));
    c.hline(kFir.x, kFir.x + kFir.w - 1, mid, gfx::kInkGrid);
    for (int k = 0; k < 8; ++k) {
        const int v = e.fir[static_cast<std::size_t>(k)];
        const int y = kFir.y + static_cast<int>((127.0 - v) / 255.0 * (kFir.h - 1));
        c.fill_rect(gfx::Rect{kFir.x + k * 26 + 6, std::min(y, mid), 14, std::abs(y - mid) + 1}, gfx::kInkText);
        widgets::small_text(c, ui, kFir.x + k * 26 + 4, kFir.y + kFir.h + 3, num(v, "%4d"), gfx::kInkDim);
    }

    // ARAM budget.
    const std::size_t used = io::aram_bytes(doc);
    const bool over = used > model::kAramBytes;
    std::snprintf(buf, sizeof buf, "ARAM %.1f / 64 KB  (DRIVER 8, ECHO %.1f)%s", static_cast<double>(used) / 1024.0,
                  static_cast<double>(model::snes_echo_bytes(e.delay)) / 1024.0, over ? "  OVER BUDGET" : "");
    widgets::text(c, ui, 1, 24, buf, over ? gfx::kInkWarn : gfx::kInkText);
    const gfx::Rect bar{px(1), py(25) + 2, kCanvasW - px(2), 8};
    c.rect(bar, gfx::kInkDim);
    const int fill = static_cast<int>(std::min<std::size_t>(used, model::kAramBytes) * static_cast<std::size_t>(bar.w - 2) / model::kAramBytes);
    c.fill_rect(gfx::Rect{bar.x + 1, bar.y + 1, fill, bar.h - 2}, over ? gfx::kInkWarn : gfx::kInkText);
    widgets::small_text(c, ui, px(1), py(26) + 4, "OVER BUDGET IS A WARNING ONLY. STRICT SNES: 8 VOICES (ALL SNES INSTRUMENTS).",
                        gfx::kInkDim);
}

} // namespace phos::ui::chip
