#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/palette.h"

namespace phos::gfx {

struct Image {
    int width = 0;
    int height = 0;
    int channels = 0; // 1 = gray, 3 = RGB
    std::vector<std::uint8_t> pixels;
};

std::optional<Image> read_png_gray(const std::filesystem::path& path);
std::optional<Image> read_png_rgb(const std::filesystem::path& path);
bool write_png(const std::filesystem::path& path, const Image& image);

// Resolves palette indices to RGB (3 bytes per pixel). Exactly what the CRT pass receives before effects.
Image canvas_to_rgb(const Canvas& canvas, const Theme& theme);

} // namespace phos::gfx
