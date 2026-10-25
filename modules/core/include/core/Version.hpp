#pragma once

#include <string_view>

namespace betacraft::core {

// BETACRAFT_VERSION comes from project(... VERSION ...) in the root CMakeLists.txt.
inline constexpr std::string_view kGameName = "Betacraft";
inline constexpr std::string_view kGameVersion = BETACRAFT_VERSION;
inline constexpr std::string_view kGameTitle = "Betacraft " BETACRAFT_VERSION;

} // namespace betacraft::core
