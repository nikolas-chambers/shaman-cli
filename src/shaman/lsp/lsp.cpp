#include "shaman/lsp/lsp.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <fstream>
#include <sstream>

#include "shaman/core/log.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::lsp {
using namespace std::chrono;
using namespace std::chrono_literals;

std::vector<ServerSpec> builtin_servers() {
  return {
      {"clangd", {"clangd", "--background-index=false"}, {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}},
      {"pyright", {"pyright-langserver", "--stdio"}, {".py", ".pyi"}},
      {"typescript", {"typescript-language-server", "--stdio"}, {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"}},
      {"gopls", {"gopls"}, {".go"}},
      {"rust-analyzer", {"rust-analyzer"}, {".rs"}},
      {"zls", {"zls"}, {".zig"}},
      {"lua", {"lua-language-server"}, {".lua"}},
  };
}

std::string uri(const fs::path& p) {
  std::string out = "file://";
  for (char c : fs::absolute(p).generic_string())
    out += (std::isalnum(static_cast<unsigned char>(c)) || std::string_view("/-_.~").find(c) != std::string_view::npos)
               ? std::string(1, c)
               : std::format("%{:02X}", static_cast<unsigned char>(c));
  return out;
}

fs::path from_uri(const std::string& u) { return str::url_decode(u.starts_with("file://") ? u.substr(7) : u); }

static std::string language_id(const fs::path& p) {
  auto e = p.extension().string();
  if (e == ".py" || e == ".pyi") return "python";
  if (e == ".ts") return "typescript";
  if (e == ".tsx") return "typescriptreact";
  if (e == ".js" || e == ".mjs" || e == ".cjs") return "javascript";
  if (e == ".jsx") return "javascriptreact";
  if (e == ".go") return "go";
  if (e == ".rs") return "rust";
  if (e == ".c" || e == ".h") return "c";
  if (e == ".zig") return "zig";
  if (e == ".lua") return "lua";
  return "cpp";
}

Result<std::unique_ptr<Client>> Client::start(const ServerSpec& spec, const fs::path& root) {
  auto child = process::Child::spawn(spec.command);
  if (!child) return std::unexpected(child.error());
  auto c = std::unique_ptr<Client>(new Client(spec, std::move(*child)));
  auto init = c->request("initialize",
                         {{"processId", nullptr},
                          {"rootUri", uri(root)},
                          {"workspaceFolders", Json::array({{{"uri", uri(root)}, {"name", root.filename().string()}}})},
                          {"capabilities",
                           {{"textDocument",
                             {{"synchronization", {{"didSave", true}}},
                              {"publishDiagnostics", {{"relatedInformation", false}}},
                              {"hover", {{"contentFormat", {"plaintext", "markdown"}}}}}},
                            {"workspace", {{"configuration", true}, {"workspaceFolders", true}}}}}},
                         20s);
  if (!init) return std::unexpected(init.error());
  c->notify("initialized", Json::object());
  log::debug(log::Cat::tool, "lsp {} started", spec.id);
  return c;
}

Client::~Client() {
  if (child_.alive()) {
    send({{"jsonrpc", "2.0"}, {"id", next_id_++}, {"method", "shutdown"}});
    notify("exit", nullptr);
  }
}

void Client::send(const Json& msg) {
  auto body = msg.dump();
  log::debug(log::Cat::mcp, "lsp {} -> {}", spec_.id, body.substr(0, 300));
  child_.write(std::format("Content-Length: {}\r\n\r\n{}", body.size(), body));
}

void Client::notify(const std::string& method, const Json& params) {
  Json msg = {{"jsonrpc", "2.0"}, {"method", method}};
  if (!params.is_null()) msg["params"] = params;
  send(msg);
}

Result<std::optional<Json>> Client::read_message(milliseconds wait) {
  size_t length = 0;
  while (true) {
    auto line = child_.read_line(wait);
    if (!line) return std::unexpected(line.error());
    if (!*line) return std::optional<Json>{};
    auto l = str::trim(**line);
    if (l.empty()) break;
    if (str::lower(l).starts_with("content-length:")) length = std::stoul(l.substr(15));
  }
  auto body = child_.read_exact(length, wait);
  if (!body) return std::unexpected(body.error());
  if (!*body) return std::optional<Json>{};
  try {
    return std::optional<Json>(Json::parse(**body));
  } catch (...) {
    return fail("lsp " + spec_.id + ": bad JSON from server");
  }
}

void Client::handle(const Json& msg) {
  auto method = msg.value("method", "");
  if (msg.contains("id") && !method.empty()) {  // server -> client request: answer so it never blocks
    Json result = nullptr;
    if (method == "workspace/configuration") {
      result = Json::array();
      for (size_t i = 0; i < msg["params"].value("items", Json::array()).size(); ++i) result.push_back(nullptr);
    }
    send({{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", result}});
    return;
  }
  if (method == "textDocument/publishDiagnostics") {
    auto& p = msg["params"];
    std::vector<Diagnostic> ds;
    for (auto& d : p.value("diagnostics", Json::array())) {
      auto start = d["range"]["start"];
      ds.push_back({start.value("line", 0) + 1, start.value("character", 0) + 1, d.value("severity", 1),
                    d.value("message", ""), d.value("source", "")});
    }
    auto u = p.value("uri", "");
    diagnostics_[u] = std::move(ds);
    fresh_[u] = true;
  }
}

Result<Json> Client::request(const std::string& method, const Json& params, milliseconds wait) {
  int id = next_id_++;
  send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
  auto deadline = steady_clock::now() + wait;
  while (steady_clock::now() < deadline) {
    auto msg = read_message(duration_cast<milliseconds>(deadline - steady_clock::now()));
    if (!msg) return std::unexpected(msg.error());
    if (!*msg) break;
    auto& m = **msg;
    if (m.contains("id") && !m.contains("method") && m["id"] == id) {
      if (m.contains("error")) return fail("lsp " + spec_.id + ": " + m["error"].value("message", "error"));
      return m.value("result", Json());
    }
    handle(m);
  }
  return fail("lsp " + spec_.id + ": timed out on " + method);
}

std::vector<Diagnostic> Client::diagnostics(const fs::path& file, const std::string& text, milliseconds wait) {
  auto u = uri(file);
  fresh_[u] = false;
  auto& version = versions_[u];
  if (version == 0) {
    notify("textDocument/didOpen", {{"textDocument", {{"uri", u}, {"languageId", language_id(file)}, {"version", ++version}, {"text", text}}}});
  } else {
    notify("textDocument/didChange", {{"textDocument", {{"uri", u}, {"version", ++version}}}, {"contentChanges", Json::array({{{"text", text}}})}});
  }
  notify("textDocument/didSave", {{"textDocument", {{"uri", u}}}, {"text", text}});
  auto deadline = steady_clock::now() + wait;
  while (!fresh_[u] && steady_clock::now() < deadline) {
    auto msg = read_message(duration_cast<milliseconds>(deadline - steady_clock::now()));
    if (!msg || !*msg) break;
    handle(**msg);
  }
  return diagnostics_[u];
}

Manager::Manager(const Config& config, fs::path root) : root_(std::move(root)), specs_(builtin_servers()) {
  auto cfg = config.raw.value("lsp", Json::object());
  disabled_ = cfg.value("disabled", false);
  timeout_ = milliseconds(cfg.value("timeout", 3000));
  auto servers = cfg.value("servers", Json::object());
  for (auto& [id, s] : servers.items()) {
    std::erase_if(specs_, [&](const ServerSpec& x) { return x.id == id && s.value("disabled", false); });
    if (s.contains("command")) {
      std::erase_if(specs_, [&](const ServerSpec& x) { return x.id == id; });
      specs_.insert(specs_.begin(), {id, s["command"].get<std::vector<std::string>>(),
                                     s.value("extensions", std::vector<std::string>{})});
    }
  }
}

Manager::~Manager() = default;

Client* Manager::client_for(const fs::path& file) {
  if (disabled_) return nullptr;
  auto ext = file.extension().string();
  for (auto& spec : specs_) {
    if (std::ranges::find(spec.extensions, ext) == spec.extensions.end()) continue;
    if (broken_[spec.id]) return nullptr;
    if (auto it = clients_.find(spec.id); it != clients_.end()) return it->second.get();
    if (!process::which(spec.command.front())) {
      broken_[spec.id] = true;
      log::debug(log::Cat::tool, "lsp {}: {} not installed", spec.id, spec.command.front());
      return nullptr;
    }
    auto c = Client::start(spec, root_);
    if (!c) {
      broken_[spec.id] = true;
      log::warn("lsp " + spec.id + ": " + c.error().message);
      return nullptr;
    }
    return (clients_[spec.id] = std::move(*c)).get();
  }
  return nullptr;
}

static std::string location(const Json& loc, const fs::path& root) {
  auto uri_str = loc.value("uri", loc.value("targetUri", ""));
  auto range = loc.contains("range") ? loc["range"] : loc.value("targetSelectionRange", Json::object());
  auto path = from_uri(uri_str).lexically_relative(root).generic_string();
  return std::format("{}:{}:{}", path, range["start"].value("line", 0) + 1, range["start"].value("character", 0) + 1);
}

Result<std::string> Manager::query(const std::string& op, const fs::path& file, int line, int column) {
  std::lock_guard lock(mu_);
  auto* c = client_for(file);
  if (!c) return fail("no language server for " + file.extension().string() + " files (see `shaman debug lsp`)");
  std::ifstream in(file);
  std::stringstream ss;
  ss << in.rdbuf();
  auto ds = c->diagnostics(file, ss.str(), timeout_);  // also opens/syncs the document
  if (op == "diagnostics") {
    std::string out;
    for (auto& d : ds)
      out += std::format("{}:{} [{}] {}\n", d.line, d.column,
                         d.severity == 1 ? "error" : d.severity == 2 ? "warning" : "info", d.message);
    return out.empty() ? "No diagnostics" : out;
  }
  Json pos = {{"textDocument", {{"uri", uri(file)}}}, {"position", {{"line", line - 1}, {"character", column - 1}}}};
  if (op == "hover") {
    auto r = c->request("textDocument/hover", pos, 10s);
    if (!r) return std::unexpected(r.error());
    if (r->is_null()) return std::string("No hover information");
    auto contents = (*r)["contents"];
    if (contents.is_string()) return contents.get<std::string>();
    if (contents.is_object()) return contents.value("value", contents.dump());
    std::string out;
    for (auto& part : contents) out += (part.is_string() ? part.get<std::string>() : part.value("value", "")) + "\n";
    return out;
  }
  if (op == "definition" || op == "references") {
    if (op == "references") pos["context"] = {{"includeDeclaration", true}};
    auto r = c->request(op == "definition" ? "textDocument/definition" : "textDocument/references", pos, 10s);
    if (!r) return std::unexpected(r.error());
    if (r->is_null() || (r->is_array() && r->empty())) return std::string("No results");
    std::string out;
    if (r->is_object()) return location(*r, root_);
    for (auto& loc : *r) out += location(loc, root_) + "\n";
    return out;
  }
  if (op == "symbols") {
    auto r = c->request("textDocument/documentSymbol", {{"textDocument", {{"uri", uri(file)}}}}, 10s);
    if (!r) return std::unexpected(r.error());
    std::string out;
    std::function<void(const Json&, int)> walk = [&](const Json& syms, int depth) {
      for (auto& s : syms) {
        auto range = s.contains("range") ? s["range"] : s["location"]["range"];
        out += std::format("{}{} (line {})\n", std::string(depth * 2, ' '), s.value("name", "?"), range["start"].value("line", 0) + 1);
        if (s.contains("children")) walk(s["children"], depth + 1);
      }
    };
    if (r->is_array()) walk(*r, 0);
    return out.empty() ? "No symbols" : out;
  }
  return fail("unknown lsp operation: " + op);
}

std::string Manager::report(const fs::path& file) {
  std::lock_guard lock(mu_);
  auto* c = client_for(file);
  if (!c) return "";
  std::ifstream in(file);
  std::stringstream ss;
  ss << in.rdbuf();
  auto ds = c->diagnostics(file, ss.str(), timeout_);
  std::vector<std::string> lines;
  for (auto& d : ds)
    if (d.severity == 1 && lines.size() < 20) lines.push_back(std::format("  {}:{} {}", d.line, d.column, d.message));
  if (lines.empty()) return "";
  auto rel = file.lexically_relative(root_).generic_string();
  return std::format("\n\nLSP errors in {} (fix these):\n{}", rel, str::join(lines, "\n"));
}

}  // namespace shaman::lsp
