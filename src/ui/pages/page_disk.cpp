// Page 2 — DISK CONTROL: folder browser for samples, import into the current instrument,
// audition (PLAN.md §7.2, docs/screens.md §3). Keyboard first; the light pen selects and opens.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/text.h"
#include "io/import.h"
#include "io/smf.h"
#include "io/instrument_io.h"
#include "io/project.h"
#include "ui/layout.h"
#include "ui/pages/pages.h"
#include "ui/ui.h"
#include "ui/widgets.h"

namespace phos::ui {

namespace {

namespace fs = std::filesystem;

constexpr int kListTop = 4;
constexpr int kListRows = 21; // rows 4..24

struct Entry {
    std::string name;
    bool dir = false;
    std::uintmax_t size = 0;
    bool project = false; // a Name.phosphor bundle: ENTER loads it
    bool instrument = false; // a Name.phinst bundle: ENTER loads it into the current instrument
};

std::string size_text(std::uintmax_t bytes) {
    char buf[16];
    if (bytes >= 1024 * 1024) {
        std::snprintf(buf, sizeof buf, "%6.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof buf, "%6.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

// Keeps the end of a long path visible: "...SAMPLES/DRUMS".
std::string tail(const std::string& s, std::size_t width) {
    return s.size() <= width ? s : "..." + s.substr(s.size() - (width - 3));
}

class DiskPage final : public Page {
public:
    std::string_view id() const override { return "2"; }
    std::string_view title() const override { return "DISK CONTROL"; }
    bool has_fields() const override { return true; }

    void enter(Ui& ui) override {
        const fs::path want = ui.app().browse_folder();
        if (want != folder_ || entries_.empty()) {
            open(ui, want);
        }
    }

    bool on_key(const InputEvent& ev, Ui& ui) override {
        if (ev.type != InputEvent::Type::Key) {
            return false;
        }
        const int n = static_cast<int>(entries_.size());
        switch (ev.key) {
        case Key::Up:
            if (sel_ == 0) {
                ui.set_focus(Focus::Command);
            } else {
                move(sel_ - 1);
            }
            return true;
        case Key::Down:
            move(std::min(sel_ + 1, n - 1));
            return true;
        case Key::PageUp:
            move(std::max(0, sel_ - kListRows));
            return true;
        case Key::PageDown:
            move(std::min(n - 1, sel_ + kListRows));
            return true;
        case Key::Home:
            move(0);
            return true;
        case Key::End:
            move(n - 1);
            return true;
        case Key::Enter:
        case Key::Right:
            activate(ui, ev.key == Key::Right);
            return true;
        case Key::Backspace:
        case Key::Left:
            parent(ui);
            return true;
        case Key::Char:
            if (ev.ch == 'A') {
                audition(ui);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void on_zone(int zone, int wheel, Ui& ui) override {
        if (wheel != 0) {
            move(std::clamp(sel_ - wheel * 3, 0, std::max(0, static_cast<int>(entries_.size()) - 1)));
            return;
        }
        if (zone == kZoneParent) {
            parent(ui);
            return;
        }
        const int index = top_ + zone;
        if (index < 0 || index >= static_cast<int>(entries_.size())) {
            return;
        }
        if (index == sel_) {
            activate(ui, false); // second click opens / imports
        } else {
            move(index);
        }
    }

    void draw(gfx::Canvas& c, const Ui& ui) const override {
        const bool fields = ui.focus() == Focus::Fields;
        widgets::text(c, ui, 1, 2, "FOLDER:");
        widgets::text(c, ui, 9, 2, tail(to_upper(folder_.string()), 46), gfx::kInkText);
        widgets::button(c, ui, 57, 2, "UP", false, kZoneParent);
        c.hline(px(1), kCanvasW - px(1), py(3) + 6, gfx::kInkGrid);

        if (!error_.empty()) {
            widgets::text(c, ui, 2, kListTop, error_, gfx::kInkWarn);
        } else if (entries_.empty()) {
            widgets::text(c, ui, 2, kListTop, folder_.empty() ? "NO FOLDER" : "NO FOLDERS OR AUDIO FILES HERE",
                          gfx::kInkDim);
        }
        for (int r = 0; r < kListRows; ++r) {
            const int index = top_ + r;
            if (index >= static_cast<int>(entries_.size())) {
                break;
            }
            const Entry& e = entries_[static_cast<std::size_t>(index)];
            const int row = kListTop + r;
            const bool selected = index == sel_;
            const gfx::Rect rect = cells(1, row, kCols - 2, 1);
            std::uint8_t ink = e.dir ? gfx::kInkText : gfx::kInkBright;
            if (selected) {
                c.fill_rect(rect, fields ? gfx::kInkBright : gfx::kInkField);
                ink = fields ? gfx::kInkBg : gfx::kInkBright;
            }
            const std::string name = e.dir ? to_upper(e.name) + "/" : to_upper(e.name);
            widgets::text(c, ui, 2, row, e.project ? "PRJ" : e.instrument ? "INS" : e.dir ? "DIR" : "   ",
                          selected && fields ? gfx::kInkBg : gfx::kInkDim);
            widgets::text(c, ui, 6, row, fit(name, 44), ink);
            if (!e.dir && !e.project && !e.instrument) { // bundles are folders: no size
                widgets::text(c, ui, 52, row, size_text(e.size), selected && fields ? gfx::kInkBg : gfx::kInkDim);
            }
            ui.add_zone(rect, ZoneOwner::Page, r);
        }
        if (static_cast<int>(entries_.size()) > kListRows) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%d-%d / %d", top_ + 1,
                          std::min(top_ + kListRows, static_cast<int>(entries_.size())), static_cast<int>(entries_.size()));
            widgets::small_text(c, ui, px(50), py(25) + 2, buf, gfx::kInkDim);
        }

        const auto& doc = ui.app().workspace().doc();
        char buf[96];
        std::snprintf(buf, sizeof buf, "ENTER = IMPORT INTO INSTRUMENT %02d (%s)   A = AUDITION   BACKSPACE = UP",
                      doc.current + 1, doc.current_instrument().name.substr(0, 10).c_str());
        widgets::small_text(c, ui, px(1), py(26) + 2, buf, gfx::kInkDim);
        const std::string& status = ui.app().import_status();
        widgets::small_text(c, ui, px(1), py(27) + 2,
                            status.empty() ? std::string("WAV AIFF FLAC MP3 OGG SAMPLES, .MID SONGS, .PHINST INSTRUMENTS, .PHOSPHOR PROJECTS")
                                           : status,
                            status.empty() ? gfx::kInkGrid : gfx::kInkBright);
    }

private:
    static constexpr int kZoneParent = 1000;

    void open(Ui& ui, const fs::path& folder) {
        entries_.clear();
        error_.clear();
        sel_ = 0;
        top_ = 0;
        folder_ = folder;
        if (folder.empty()) {
            return;
        }
        std::error_code ec;
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') {
                continue;
            }
            std::error_code e2;
            if (io::is_project_bundle(it->path())) {
                entries_.push_back({name, false, 0, true});
            } else if (io::is_instrument_bundle(it->path())) {
                entries_.push_back({name, false, 0, false, true});
            } else if (it->is_directory(e2)) {
                entries_.push_back({name, true, 0});
            } else if (io::is_audio_file(it->path()) || io::is_midi_file(it->path())) {
                entries_.push_back({name, false, it->file_size(e2)});
            }
        }
        if (ec) {
            error_ = "CANNOT READ THIS FOLDER";
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.project != b.project) {
                return a.project; // projects first, then instruments, folders, files
            }
            if (a.instrument != b.instrument) {
                return a.instrument;
            }
            if (a.dir != b.dir) {
                return a.dir;
            }
            return to_upper(a.name) < to_upper(b.name);
        });
        ui.app().set_browse_folder(folder);
    }

    void move(int index) {
        if (entries_.empty()) {
            return;
        }
        sel_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
        if (sel_ < top_) {
            top_ = sel_;
        } else if (sel_ >= top_ + kListRows) {
            top_ = sel_ - kListRows + 1;
        }
    }

    void parent(Ui& ui) {
        if (folder_.has_parent_path() && folder_.parent_path() != folder_) {
            const std::string from = folder_.filename().string();
            open(ui, folder_.parent_path());
            for (std::size_t i = 0; i < entries_.size(); ++i) {
                if (entries_[i].name == from) {
                    move(static_cast<int>(i)); // keep the cursor on the folder we came from
                }
            }
        }
    }

    void activate(Ui& ui, bool folders_only) {
        if (sel_ < 0 || sel_ >= static_cast<int>(entries_.size())) {
            return;
        }
        const Entry& e = entries_[static_cast<std::size_t>(sel_)];
        if (e.project && !folders_only) {
            ui.execute("LOAD " + (folder_ / e.name).string());
            return;
        }
        if (e.instrument && !folders_only) {
            ui.execute("IMPORT " + (folder_ / e.name).string()); // into the current instrument (undoable)
            return;
        }
        if (e.dir) {
            open(ui, folder_ / e.name);
        } else if (!folders_only && io::is_midi_file(e.name)) {
            ui.execute("IMPORT " + (folder_ / e.name).string()); // replaces the song (undoable)
        } else if (!folders_only) {
            ui.app().import_sample(folder_ / e.name, ui.app().workspace().doc().current);
        }
    }

    void audition(Ui& ui) {
        if (sel_ >= 0 && sel_ < static_cast<int>(entries_.size()) && !entries_[static_cast<std::size_t>(sel_)].dir &&
            !io::is_midi_file(entries_[static_cast<std::size_t>(sel_)].name)) {
            ui.app().audition_file(folder_ / entries_[static_cast<std::size_t>(sel_)].name);
            ui.message("AUDITION " + to_upper(entries_[static_cast<std::size_t>(sel_)].name));
        }
    }

    fs::path folder_;
    std::vector<Entry> entries_;
    std::string error_;
    int sel_ = 0;
    int top_ = 0;
};

} // namespace

std::unique_ptr<Page> make_disk_page() { return std::make_unique<DiskPage>(); }

} // namespace phos::ui
