#pragma once

#include <string_view>

namespace aurora {

// Product identity. Keep this separate from upstream launchers and from the
// archived Java tree; these values are AuroraLauncher's own identity.
inline constexpr std::string_view kLauncherName = "AuroraLauncher";
inline constexpr std::string_view kLauncherVersion = "0.3.0";
inline constexpr std::string_view kLauncherVendor = "AuroraLauncher Project";
inline constexpr std::string_view kLauncherUserAgent = "AuroraLauncher/0.3.0";
inline constexpr std::string_view kLauncherBrand = "AuroraLauncher";
// Set this only when the project has its own real website. Do not point at a
// third-party launcher's site.
inline constexpr std::string_view kLauncherWebsite = "";

// Microsoft OAuth client IDs must come from AuroraLauncher's own app
// registration. Never substitute an ID belonging to HMCL or another launcher.
inline constexpr std::string_view kMicrosoftClientIdConfigKey = "authClientId";

} // namespace aurora
