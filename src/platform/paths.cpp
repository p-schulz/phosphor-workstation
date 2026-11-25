#include "platform/paths.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace phos::platform {

std::filesystem::path exe_dir() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return std::filesystem::current_path();
        }
        if (len < buf.size()) {
            buf.resize(len);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return std::filesystem::current_path();
    }
    std::error_code ec;
    auto p = std::filesystem::canonical(buf, ec);
    return ec ? std::filesystem::path(buf).parent_path() : p.parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#endif
}

std::filesystem::path default_config_dir() {
#if defined(_WIN32)
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata && *appdata) {
        return std::filesystem::path(appdata) / "Phosphor";
    }
    return exe_dir() / "config";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "Phosphor";
    }
    return exe_dir() / "config";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::filesystem::path(xdg) / "phosphor";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "phosphor";
    }
    return exe_dir() / "config";
#endif
}

} // namespace phos::platform
