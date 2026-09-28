// POSIX (Linux, macOS) implementation of shaman::process.
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::process {
namespace fs = std::filesystem;
using namespace std::chrono;

namespace {

std::vector<char*> make_argv(const std::vector<std::string>& argv) {
  std::vector<char*> out;
  for (auto& a : argv) out.push_back(const_cast<char*>(a.c_str()));
  out.push_back(nullptr);
  return out;
}

}  // namespace

std::optional<fs::path> which(const std::string& name) {
  const char* path = std::getenv("PATH");
  if (!path) return std::nullopt;
  for (auto& dir : str::split(path, ':')) {
    if (dir.empty()) continue;
    fs::path p = fs::path(dir) / name;
    if (access(p.c_str(), X_OK) == 0) return p;
  }
  return std::nullopt;
}

Result<Output> run(const std::vector<std::string>& argv, const Options& opts) {
  if (argv.empty()) return fail("empty command");
  int pipefd[2];
  if (pipe(pipefd) != 0) return fail(std::string("pipe: ") + std::strerror(errno), errno);

  pid_t pid = fork();
  if (pid < 0) return fail(std::string("fork: ") + std::strerror(errno), errno);
  if (pid == 0) {
    setpgid(0, 0);
    int devnull = open("/dev/null", O_RDONLY);
    dup2(devnull, STDIN_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    if (!opts.cwd.empty() && chdir(opts.cwd.c_str()) != 0) _exit(126);
    auto args = make_argv(argv);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(pipefd[1]);
  fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

  Output out;
  auto deadline = steady_clock::now() + opts.timeout;
  char buf[8192];
  bool open_pipe = true;
  while (open_pipe) {
    if (opts.cancel && opts.cancel->load()) {
      out.cancelled = true;
      break;
    }
    if (steady_clock::now() >= deadline) {
      out.timed_out = true;
      break;
    }
    pollfd pfd{pipefd[0], POLLIN, 0};
    if (poll(&pfd, 1, 100) > 0) {
      ssize_t n = read(pipefd[0], buf, sizeof buf);
      if (n > 0) {
        if (out.output.size() < opts.max_output)
          out.output.append(buf, std::min<size_t>(n, opts.max_output - out.output.size()));
      } else if (n == 0 || errno != EAGAIN) {
        open_pipe = false;
      }
    }
  }
  close(pipefd[0]);
  if (out.timed_out || out.cancelled) {
    kill(-pid, SIGTERM);
    usleep(200'000);
    kill(-pid, SIGKILL);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) out.exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) out.exit_code = 128 + WTERMSIG(status);
  return out;
}

Result<Output> shell(const std::string& command, const Options& opts) {
  std::string sh = which("bash") ? "bash" : "sh";
  return run({sh, "-c", command}, opts);
}

Result<Child> Child::spawn(const std::vector<std::string>& argv,
                           const std::vector<std::pair<std::string, std::string>>& env) {
  if (argv.empty()) return fail("empty command");
  int to_child[2], from_child[2];
  if (pipe(to_child) != 0 || pipe(from_child) != 0) return fail("pipe failed", errno);
  pid_t pid = fork();
  if (pid < 0) return fail("fork failed", errno);
  if (pid == 0) {
    setpgid(0, 0);
    dup2(to_child[0], STDIN_FILENO);
    dup2(from_child[1], STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);  // MCP servers log to stderr; keep our terminal clean
    close(to_child[1]);
    close(from_child[0]);
    for (auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
    auto args = make_argv(argv);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(to_child[0]);
  close(from_child[1]);
  Child c;
  c.pid_ = pid;
  c.in_ = to_child[1];
  c.out_ = from_child[0];
  return c;
}

Child::Child(Child&& o) noexcept { *this = std::move(o); }

Child& Child::operator=(Child&& o) noexcept {
  std::swap(pid_, o.pid_);
  std::swap(in_, o.in_);
  std::swap(out_, o.out_);
  std::swap(buf_, o.buf_);
  return *this;
}

Child::~Child() { kill(); }

Result<void> Child::write_line(const std::string& line) {
  std::string data = line + "\n";
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(in_, data.data() + off, data.size() - off);
    if (n <= 0) return fail("write to child failed", errno);
    off += size_t(n);
  }
  return {};
}

Result<std::optional<std::string>> Child::read_line(milliseconds timeout) {
  auto deadline = steady_clock::now() + timeout;
  while (true) {
    if (auto nl = buf_.find('\n'); nl != std::string::npos) {
      std::string line = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      return line;
    }
    auto left = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
    if (left <= 0) return std::optional<std::string>{};
    pollfd pfd{out_, POLLIN, 0};
    if (poll(&pfd, 1, int(left)) <= 0) continue;
    char buf[8192];
    ssize_t n = read(out_, buf, sizeof buf);
    if (n <= 0) return fail("child closed its output");
    buf_.append(buf, size_t(n));
  }
}

void Child::kill() {
  if (in_ >= 0) close(in_);
  if (out_ >= 0) close(out_);
  if (pid_ > 0) {
    ::kill(-pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
  }
  pid_ = in_ = out_ = -1;
}

}  // namespace shaman::process
