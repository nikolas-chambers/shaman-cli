#include "shaman/cli/args.hpp"

#include <algorithm>

namespace shaman::cli {

std::optional<std::string> Args::get(const std::string& name) const {
  auto it = options.find(name);
  if (it == options.end()) return std::nullopt;
  return it->second;
}

Args parse_args(int argc, char** argv, const std::vector<std::string>& boolean,
                const std::map<std::string, std::string>& aliases) {
  Args a;
  bool rest = false;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    if (rest || s == "-" || !s.starts_with("-")) {
      a.positional.push_back(s);
      continue;
    }
    if (s == "--") {
      rest = true;
      continue;
    }
    std::string name, value;
    bool has_value = false;
    if (s.starts_with("--")) {
      name = s.substr(2);
      if (auto eq = name.find('='); eq != std::string::npos) {
        value = name.substr(eq + 1);
        name = name.substr(0, eq);
        has_value = true;
      }
    } else {
      auto it = aliases.find(s.substr(1));
      name = it != aliases.end() ? it->second : s.substr(1);
    }
    bool is_bool = std::ranges::find(boolean, name) != boolean.end();
    if (!has_value && !is_bool && i + 1 < argc) value = argv[++i];
    a.options[name] = value;
  }
  return a;
}

}  // namespace shaman::cli
