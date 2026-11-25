#include "gfx/font.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gfx/image_io.h"

namespace phos::gfx {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool parse_int(std::string_view s, int& out) {
    s = trim(s);
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    const auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, out, base);
    return ec == std::errc{} && ptr == end;
}

bool fail(std::string* error, std::string msg) {
    if (error) {
        *error = std::move(msg);
    }
    return false;
}

} // namespace

std::span<const std::uint8_t> BitmapFont::glyph_rows(unsigned char c) const {
    const auto h = static_cast<std::size_t>(cell_h);
    if (rows.size() < 256 * h) {
        return {};
    }
    return std::span<const std::uint8_t>(rows).subspan(static_cast<std::size_t>(c) * h, h);
}

std::optional<BitmapFont> parse_font_source(std::string_view text, std::string* error) {
    BitmapFont font;
    int top = 0;
    int lines = -1;
    bool fold_lowercase = false;
    int current = -1;
    int current_line = 0;
    int line_no = 0;

    auto ensure_rows = [&] {
        if (font.rows.empty()) {
            font.rows.assign(256 * static_cast<std::size_t>(font.cell_h), 0);
        }
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        const std::string_view line = trim(raw);
        const std::string where = "line " + std::to_string(line_no) + ": ";
        const bool is_data = !line.empty() && line.find_first_not_of("#.") == std::string_view::npos;
        if (line.empty() || (line.front() == '#' && !(is_data && current >= 0))) {
            continue; // blank line or comment; rows of only '#'/'.' inside a glyph are data
        }
        if (is_data) {
            if (current < 0) {
                fail(error, where + "glyph row outside a glyph block");
                return std::nullopt;
            }
            if (lines >= 0 && current_line >= lines) {
                fail(error, where + "too many rows in glyph");
                return std::nullopt;
            }
            if (static_cast<int>(line.size()) > font.cell_w) {
                fail(error, where + "row wider than cell");
                return std::nullopt;
            }
            const int row = top + current_line;
            if (row >= font.cell_h) {
                fail(error, where + "row below cell");
                return std::nullopt;
            }
            std::uint8_t bits = 0;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '#') {
                    bits = static_cast<std::uint8_t>(bits | (0x80u >> i));
                }
            }
            font.rows[static_cast<std::size_t>(current) * static_cast<std::size_t>(font.cell_h) +
                      static_cast<std::size_t>(row)] = bits;
            ++current_line;
            continue;
        }
        const std::size_t sp = line.find(' ');
        const std::string_view key = line.substr(0, sp);
        const std::string_view rest = sp == std::string_view::npos ? std::string_view{} : trim(line.substr(sp + 1));
        if (key == "name") {
            font.name = std::string(rest);
        } else if (key == "cell") {
            const std::size_t sp2 = rest.find(' ');
            if (sp2 == std::string_view::npos || !parse_int(rest.substr(0, sp2), font.cell_w) ||
                !parse_int(rest.substr(sp2 + 1), font.cell_h) || font.cell_w < 1 || font.cell_w > 8 ||
                font.cell_h < 1 || font.cell_h > 32) {
                fail(error, where + "bad cell size");
                return std::nullopt;
            }
        } else if (key == "top") {
            if (!parse_int(rest, top)) {
                fail(error, where + "bad top");
                return std::nullopt;
            }
        } else if (key == "lines") {
            if (!parse_int(rest, lines)) {
                fail(error, where + "bad lines");
                return std::nullopt;
            }
        } else if (key == "baseline") {
            if (!parse_int(rest, font.baseline)) {
                fail(error, where + "bad baseline");
                return std::nullopt;
            }
        } else if (key == "fold_lowercase") {
            fold_lowercase = rest == "1";
        } else if (key == "glyph") {
            ensure_rows();
            const std::string_view code = rest.substr(0, rest.find(' '));
            if (!parse_int(code, current) || current < 0 || current > 255) {
                fail(error, where + "bad glyph code");
                return std::nullopt;
            }
            if (font.present[static_cast<std::size_t>(current)]) {
                fail(error, where + "duplicate glyph");
                return std::nullopt;
            }
            font.present[static_cast<std::size_t>(current)] = true;
            current_line = 0;
        } else {
            fail(error, where + "unknown key '" + std::string(key) + "'");
            return std::nullopt;
        }
    }
    ensure_rows();
    if (fold_lowercase) {
        for (int c = 'a'; c <= 'z'; ++c) {
            const int upper = c - 'a' + 'A';
            if (!font.present[static_cast<std::size_t>(c)] && font.present[static_cast<std::size_t>(upper)]) {
                for (int r = 0; r < font.cell_h; ++r) {
                    font.rows[static_cast<std::size_t>(c * font.cell_h + r)] =
                        font.rows[static_cast<std::size_t>(upper * font.cell_h + r)];
                }
                font.present[static_cast<std::size_t>(c)] = true;
            }
        }
    }
    return font;
}

std::vector<std::uint8_t> font_to_atlas(const BitmapFont& font, int* atlas_w, int* atlas_h) {
    const int w = font.cell_w * 16;
    const int h = font.cell_h * 16;
    std::vector<std::uint8_t> gray(static_cast<std::size_t>(w * h), 0);
    for (int c = 0; c < 256; ++c) {
        const auto rows = font.glyph_rows(static_cast<unsigned char>(c));
        const int ox = (c % 16) * font.cell_w;
        const int oy = (c / 16) * font.cell_h;
        for (int r = 0; r < font.cell_h; ++r) {
            for (int col = 0; col < font.cell_w; ++col) {
                if (rows[static_cast<std::size_t>(r)] & (0x80u >> col)) {
                    gray[static_cast<std::size_t>((oy + r) * w + ox + col)] = 255;
                }
            }
        }
    }
    *atlas_w = w;
    *atlas_h = h;
    return gray;
}

std::string font_metrics_json(const BitmapFont& font) {
    nlohmann::json j;
    j["name"] = font.name;
    j["cell_w"] = font.cell_w;
    j["cell_h"] = font.cell_h;
    j["baseline"] = font.baseline;
    j["columns"] = 16;
    std::vector<int> codes;
    for (int c = 0; c < 256; ++c) {
        if (font.present[static_cast<std::size_t>(c)]) {
            codes.push_back(c);
        }
    }
    j["glyphs"] = codes;
    return j.dump(1) + "\n";
}

std::optional<BitmapFont> font_from_atlas(std::span<const std::uint8_t> gray, int atlas_w, int atlas_h,
                                          std::string_view metrics_json, std::string* error) {
    const auto j = nlohmann::json::parse(metrics_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        fail(error, "font metrics: invalid JSON");
        return std::nullopt;
    }
    BitmapFont font;
    font.name = j.value("name", std::string{});
    font.cell_w = j.value("cell_w", 8);
    font.cell_h = j.value("cell_h", 12);
    font.baseline = j.value("baseline", font.cell_h - 1);
    if (font.cell_w < 1 || font.cell_w > 8 || font.cell_h < 1 || font.cell_h > 32 || atlas_w != font.cell_w * 16 ||
        atlas_h != font.cell_h * 16 || gray.size() < static_cast<std::size_t>(atlas_w * atlas_h)) {
        fail(error, "font atlas: size does not match metrics");
        return std::nullopt;
    }
    font.rows.assign(256 * static_cast<std::size_t>(font.cell_h), 0);
    if (j.contains("glyphs") && j["glyphs"].is_array()) {
        for (const auto& code : j["glyphs"]) {
            if (code.is_number_integer() && code.get<int>() >= 0 && code.get<int>() < 256) {
                font.present[static_cast<std::size_t>(code.get<int>())] = true;
            }
        }
    }
    for (int c = 0; c < 256; ++c) {
        const int ox = (c % 16) * font.cell_w;
        const int oy = (c / 16) * font.cell_h;
        for (int r = 0; r < font.cell_h; ++r) {
            std::uint8_t bits = 0;
            for (int col = 0; col < font.cell_w; ++col) {
                if (gray[static_cast<std::size_t>((oy + r) * atlas_w + ox + col)] >= 128) {
                    bits = static_cast<std::uint8_t>(bits | (0x80u >> col));
                }
            }
            font.rows[static_cast<std::size_t>(c * font.cell_h + r)] = bits;
        }
    }
    return font;
}

std::optional<BitmapFont> load_font(const std::filesystem::path& base, std::string* error) {
    std::filesystem::path png = base;
    png += ".png";
    std::filesystem::path json = base;
    json += ".json";
    const auto image = read_png_gray(png);
    if (!image) {
        fail(error, "cannot read " + png.string());
        return std::nullopt;
    }
    std::ifstream in(json, std::ios::binary);
    if (!in) {
        fail(error, "cannot read " + json.string());
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return font_from_atlas(image->pixels, image->width, image->height, ss.str(), error);
}

int draw_text(Canvas& canvas, const BitmapFont& font, int x, int y, std::string_view text, std::uint8_t ink,
              int advance) {
    if (advance <= 0) {
        advance = font.cell_w;
    }
    for (const char ch : text) {
        auto c = static_cast<unsigned char>(ch);
        if (!font.has(c)) {
            c = '?';
        }
        if (c != ' ') {
            canvas.blit_rows(x, y, font.glyph_rows(c), font.cell_w, ink);
        }
        x += advance;
    }
    return x;
}

int draw_text_bg(Canvas& canvas, const BitmapFont& font, int x, int y, std::string_view text, std::uint8_t ink,
                 std::uint8_t bg, int advance) {
    if (advance <= 0) {
        advance = font.cell_w;
    }
    canvas.fill_rect(Rect{x, y, advance * static_cast<int>(text.size()), font.cell_h}, bg);
    return draw_text(canvas, font, x, y, text, ink, advance);
}

} // namespace phos::gfx
