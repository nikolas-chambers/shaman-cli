#include "shaman/cli/app.hpp"

#include "shaman/core/log.hpp"
#include "shaman/core/paths.hpp"

namespace shaman::cli {

Result<std::unique_ptr<App>> App::create(const std::filesystem::path& cwd, bool with_runtime) {
  auto app = std::make_unique<App>();
  app->cwd = cwd;
  app->root = paths::project_root(cwd);
  auto cfg = Config::load(cwd, app->root);
  if (!cfg) return std::unexpected(cfg.error());
  app->config = std::move(*cfg);
  app->auth = std::make_unique<auth::Store>(auth::Store::default_path());
  app->providers = std::make_unique<provider::Registry>(app->config, *app->auth);
  app->agents = std::make_unique<agent::Registry>(app->config, app->root);
  tool::register_builtins(app->tools, app->root);
  app->lsp = std::make_unique<lsp::Manager>(app->config, app->root);
  if (with_runtime) {
    app->mcp = mcp::load(app->config, app->tools);
    app->plugins.load(app->config, app->root, app->tools);
  }
  app->store = std::make_unique<session::Store>(paths::data_dir() / "projects" / paths::project_id(app->root) / "sessions");
  log::debug(log::Cat::session, "project {} -> {}", app->root.string(), app->store->dir().string());
  return app;
}

session::Services App::services(permission::Asker asker, std::atomic<bool>* cancel, bool allow_all,
                                std::function<Result<std::string>(const tool::Question&)> question) {
  return {root, &config, providers.get(), agents.get(), &tools, store.get(), std::move(asker), cancel, allow_all,
          lsp.get(), &plugins, std::move(question)};
}

}  // namespace shaman::cli
