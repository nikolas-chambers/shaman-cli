#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "shaman/core/result.hpp"
#include "shaman/llm/message.hpp"

namespace shaman::session {

namespace fs = std::filesystem;

struct Info {
  std::string id;
  std::string title;
  std::string agent;
  std::string model;
  std::string parent_id;  // set for subagent sessions
  int64_t created = 0;
  int64_t updated = 0;
  llm::Usage usage;       // cumulative
  double cost = 0;
  std::vector<std::string> snapshots;  // git tree ids, one per user turn, for /undo
};

// Sessions live under $XDG_DATA_HOME/shaman/projects/<project-id>/sessions/<id>/:
//   info.json       metadata, rewritten atomically
//   messages.jsonl  one message per line, append-only (a crash loses at most
//                   the line being written, never the session)
class Store {
 public:
  explicit Store(fs::path dir) : dir_(std::move(dir)) {}

  Result<Info> create(std::string agent, std::string model, std::string parent_id = "");
  Result<Info> get(const std::string& id) const;
  Result<void> save(const Info& info) const;
  Result<void> append(const std::string& id, const llm::Message& m) const;
  Result<std::vector<llm::Message>> messages(const std::string& id) const;
  Result<void> replace_messages(const std::string& id, const std::vector<llm::Message>& ms) const;
  std::vector<Info> list() const;  // newest first, top-level only
  std::optional<Info> latest() const;
  const fs::path& dir() const { return dir_; }

 private:
  fs::path dir_;
};

Json to_json(const Info& i);
Info info_from_json(const Json& j);

}  // namespace shaman::session
