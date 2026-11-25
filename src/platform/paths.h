#pragma once

#include <filesystem>

namespace phos::platform {

// Directory holding the running executable (assets/, shaders/, conf/ are copied next to it).
std::filesystem::path exe_dir();

// Per-user settings folder: macOS ~/Library/Application Support/Phosphor, Windows %APPDATA%\Phosphor,
// Linux $XDG_CONFIG_HOME/phosphor. Not created here.
std::filesystem::path default_config_dir();

} // namespace phos::platform
