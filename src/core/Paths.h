#pragma once

#include <filesystem>

namespace openpedal {

// The user's pedal folder. Definitions dropped here are picked up by the plugin and the CLI.
//   macOS:   ~/Music/OpenPedal/pedals
//   Linux:   $XDG_DATA_HOME/open-pedal/pedals or ~/.local/share/open-pedal/pedals
//   Windows: %APPDATA%/OpenPedal/pedals
// OPENPEDAL_PEDALS_DIR overrides all of the above.
std::filesystem::path defaultUserPedalsDir();

} // namespace openpedal
