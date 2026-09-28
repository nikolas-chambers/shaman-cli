#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace shaman::cli {

// Tiny argument parser: `--name value`, `--name=value`, boolean `--flag`,
// short aliases, and positionals. Flags may appear anywhere.
struct Args {
  std::vector<std::string> positional;
  std::map<std::string, std::string> options;

  bool has(const std::string& name) const { return options.contains(name); }
  std::optional<std::string> get(const std::string& name) const;
};

// `boolean` lists flags that never take a value.
Args parse_args(int argc, char** argv, const std::vector<std::string>& boolean,
                const std::map<std::string, std::string>& aliases);

}  // namespace shaman::cli
