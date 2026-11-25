#include "core/text.h"

#include <cctype>

namespace phos {

std::string to_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string> split_words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') {
            ++i;
        }
        if (i > start) {
            out.emplace_back(s.substr(start, i - start));
        }
    }
    return out;
}

std::string fit(std::string_view s, std::size_t width) {
    std::string out(s.substr(0, width));
    out.resize(width, ' ');
    return out;
}

std::string fit_right(std::string_view s, std::size_t width) {
    if (s.size() >= width) {
        return std::string(s.substr(s.size() - width));
    }
    return std::string(width - s.size(), ' ') + std::string(s);
}

} // namespace phos
