#include "core/Paths.h"

#include <cstdlib>

namespace openpedal {

namespace {

std::filesystem::path envPath(const char* name)
{
    if (const char* v = std::getenv(name); v && *v)
        return std::filesystem::path(v);
    return {};
}

} // namespace

std::filesystem::path defaultUserPedalsDir()
{
    if (auto o = envPath("OPENPEDAL_PEDALS_DIR"); !o.empty())
        return o;

#if defined(_WIN32)
    if (auto appdata = envPath("APPDATA"); !appdata.empty())
        return appdata / "OpenPedal" / "pedals";
    return std::filesystem::path("OpenPedal") / "pedals";
#elif defined(__APPLE__)
    return envPath("HOME") / "Music" / "OpenPedal" / "pedals";
#else
    if (auto xdg = envPath("XDG_DATA_HOME"); !xdg.empty())
        return xdg / "open-pedal" / "pedals";
    return envPath("HOME") / ".local" / "share" / "open-pedal" / "pedals";
#endif
}

} // namespace openpedal
