#pragma once

#include <filesystem>
#include <string>

#include "shaman/agent/agent.hpp"
#include "shaman/config/config.hpp"

namespace shaman::session {

// Assemble the system prompt: base instructions, the agent's own prompt, an
// environment block, then project instructions (AGENTS.md, SHAMAN.md,
// CLAUDE.md and config `instructions`). `shaman debug prompt` prints this.
std::string system_prompt(const agent::Agent& agent, const Config& config, const std::filesystem::path& root,
                          const std::string& model_ref);

}  // namespace shaman::session
