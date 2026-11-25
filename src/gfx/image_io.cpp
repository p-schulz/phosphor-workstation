#include "gfx/image_io.h"

#include <cstdio>

#include <stb_image.h>
#include <stb_image_write.h>

namespace phos::gfx {

namespace {

// stb takes narrow paths; on Windows go through _wfopen so UTF-8/wide paths work.
std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#if defined(_WIN32)
    const std::wstring wmode(mode, mode + std::char_traits<char>::length(mode));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::optional<Image> read_png(const std::filesystem::path& path, int channels) {
    std::FILE* f = open_file(path, "rb");
    if (!f) {
        return std::nullopt;
    }
    int w = 0;
    int h = 0;
    int n = 0;
    stbi_uc* data = stbi_load_from_file(f, &w, &h, &n, channels);
    std::fclose(f);
    if (!data) {
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.channels = channels;
    img.pixels.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * channels);
    stbi_image_free(data);
    return img;
}

void write_callback(void* context, void* data, int size) {
    std::fwrite(data, 1, static_cast<std::size_t>(size), static_cast<std::FILE*>(context));
}

} // namespace

std::optional<Image> read_png_gray(const std::filesystem::path& path) { return read_png(path, 1); }

std::optional<Image> read_png_rgb(const std::filesystem::path& path) { return read_png(path, 3); }

bool write_png(const std::filesystem::path& path, const Image& image) {
    if (image.width <= 0 || image.height <= 0 || (image.channels != 1 && image.channels != 3 && image.channels != 4)) {
        return false;
    }
    std::FILE* f = open_file(path, "wb");
    if (!f) {
        return false;
    }
    const int ok = stbi_write_png_to_func(write_callback, f, image.width, image.height, image.channels,
                                          image.pixels.data(), image.width * image.channels);
    const bool closed = std::fclose(f) == 0;
    return ok != 0 && closed;
}

Image canvas_to_rgb(const Canvas& canvas, const Theme& theme) {
    Image img;
    img.width = canvas.width();
    img.height = canvas.height();
    img.channels = 3;
    img.pixels.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3);
    std::size_t o = 0;
    for (const std::uint8_t idx : canvas.pixels()) {
        const Rgb c = theme.colors[idx & 0x0F];
        img.pixels[o++] = c.r;
        img.pixels[o++] = c.g;
        img.pixels[o++] = c.b;
    }
    return img;
}

} // namespace phos::gfx
