#pragma once

#include <string>

// `shaman upgrade [version]`: replace the running binary with a release from
// GitHub (nikolas-chambers/shaman-cli, or $SHAMAN_UPDATE_REPO). Assets are
// named shaman-<os>-<arch>[.exe], as produced by .github/workflows/release.yml.
namespace shaman::update {

std::string asset_name();  // for this platform
int upgrade(const std::string& version, bool check_only);

}  // namespace shaman::update
