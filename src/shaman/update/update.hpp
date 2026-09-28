#pragma once

#include <optional>
#include <string>

// `shaman upgrade [version]`: replace the running binary with a release from
// GitHub (nikolas-chambers/shaman-cli, or $SHAMAN_UPDATE_REPO). Assets are
// named shaman-<os>-<arch>[.exe], as produced by .github/workflows/release.yml.
namespace shaman::update {

std::string asset_name();  // for this platform
int upgrade(const std::string& version, bool check_only);

// The latest release tag when it is newer than this build. Checks GitHub at most once a day (cached under the
// cache dir, 3 s timeout) and never when "update_check" is false or SHAMAN_NO_UPDATE_CHECK is set. Blocking:
// call it from a background thread.
std::optional<std::string> newer_version(bool enabled = true);

}  // namespace shaman::update
