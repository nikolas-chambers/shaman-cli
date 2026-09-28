// Windows implementation of shaman::process: pipes, job objects (so a whole process tree can be stopped),
// and polling reads, since anonymous pipes have no non-blocking mode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstring>

#include <cstdlib>

#include "shaman/core/process.hpp"
#include "shaman/core/strings.hpp"

namespace shaman::process {
namespace fs = std::filesystem;
using namespace std::chrono;

namespace {

std::string quote(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string command_line(const std::vector<std::string>& argv) {
  std::vector<std::string> parts;
  for (auto& a : argv) parts.push_back(quote(a));
  return str::join(parts, " ");
}

}  // namespace

std::optional<fs::path> which(const std::string& name) {
  const char* path = std::getenv("PATH");
  if (!path) return std::nullopt;
  for (auto& dir : str::split(path, ';')) {
    for (const char* ext : {"", ".exe", ".cmd", ".bat"}) {
      fs::path p = fs::path(dir) / (name + ext);
      std::error_code ec;
      if (fs::is_regular_file(p, ec)) return p;
    }
  }
  return std::nullopt;
}

Result<Output> run(const std::vector<std::string>& argv, const Options& opts) {
  if (argv.empty()) return fail("empty command");
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd, wr, erd = nullptr, ewr = nullptr, in = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return fail("CreatePipe failed", int(GetLastError()));
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  if (opts.separate_stderr) {
    if (!CreatePipe(&erd, &ewr, &sa, 0)) return fail("CreatePipe failed", int(GetLastError()));
    SetHandleInformation(erd, HANDLE_FLAG_INHERIT, 0);
  }
  // stdin from a temp file (deleted on close), or nothing
  if (opts.input) {
    char dir[MAX_PATH], path[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    GetTempFileNameA(dir, "shm", 0, path);
    in = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (in != INVALID_HANDLE_VALUE) {
      DWORD n = 0;
      WriteFile(in, opts.input->data(), DWORD(opts.input->size()), &n, nullptr);
      SetFilePointer(in, 0, nullptr, FILE_BEGIN);
    } else {
      in = nullptr;
    }
  }
  if (!in) in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

  // environment: ours plus the extras
  std::string env_block;
  if (!opts.env.empty()) {
    char* cur = GetEnvironmentStringsA();
    for (char* p = cur; *p; p += std::strlen(p) + 1) env_block.append(p).push_back('\0');
    FreeEnvironmentStringsA(cur);
    for (auto& [k, v] : opts.env) env_block.append(k + "=" + v).push_back('\0');
    env_block.push_back('\0');
  }

  STARTUPINFOA si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = opts.separate_stderr ? ewr : wr;
  si.hStdInput = in;
  PROCESS_INFORMATION pi{};
  std::string cmd = command_line(argv);
  std::string cwd = opts.cwd.string();
  // A job object lets us kill the whole process tree on timeout.
  HANDLE job = CreateJobObjectA(nullptr, nullptr);
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                      env_block.empty() ? nullptr : env_block.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    CloseHandle(rd), CloseHandle(wr), CloseHandle(job), CloseHandle(in);
    if (erd) CloseHandle(erd), CloseHandle(ewr);
    return fail("CreateProcess failed: " + cmd, int(GetLastError()));
  }
  AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(wr);
  if (ewr) CloseHandle(ewr);

  Output out;
  auto deadline = steady_clock::now() + opts.timeout;
  char buf[8192];
  auto drain = [&](HANDLE h, std::string& into) -> bool {  // false once the pipe is closed
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
    if (avail == 0) return true;
    DWORD n = 0;
    if (!ReadFile(h, buf, sizeof buf, &n, nullptr) || n == 0) return false;
    if (into.size() < opts.max_output) into.append(buf, std::min<size_t>(n, opts.max_output - into.size()));
    return true;
  };
  bool out_open = true, err_open = erd != nullptr;
  while (out_open || err_open) {
    if (opts.cancel && opts.cancel->load()) { out.cancelled = true; break; }
    if (steady_clock::now() >= deadline) { out.timed_out = true; break; }
    size_t before = out.output.size() + out.error.size();
    if (out_open) out_open = drain(rd, out.output);
    if (err_open) err_open = drain(erd, out.error);
    if (out.output.size() + out.error.size() == before && WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
      for (bool more = true; more;) {  // exited: collect what is left in the pipes
        size_t n0 = out.output.size() + out.error.size();
        if (out_open) out_open = drain(rd, out.output);
        if (err_open) err_open = drain(erd, out.error);
        more = (out_open || err_open) && out.output.size() + out.error.size() != n0;
      }
      break;
    }
  }
  if (out.timed_out || out.cancelled) TerminateJobObject(job, 1);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  out.exit_code = int(code);
  CloseHandle(rd), CloseHandle(pi.hProcess), CloseHandle(pi.hThread), CloseHandle(job), CloseHandle(in);
  if (erd) CloseHandle(erd);
  return out;
}

bool termux() { return false; }

void open_url(const std::string& url) {
  if (const char* o = std::getenv("SHAMAN_OPEN"); o && *o) {
    shell(std::string(o) + " \"" + url + "\"", {.timeout = std::chrono::seconds(10)});
    return;
  }
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

Result<Output> shell(const std::string& command, const Options& opts) {
  if (which("pwsh")) return run({"pwsh", "-NoProfile", "-Command", command}, opts);
  return run({"cmd.exe", "/C", command}, opts);
}

namespace {
HANDLE H(native_handle h) { return reinterpret_cast<HANDLE>(h); }
native_handle N(HANDLE h) { return reinterpret_cast<native_handle>(h); }
bool valid(native_handle h) { return h != -1 && h != 0; }
void close_handle(native_handle& h) {
  if (valid(h)) CloseHandle(H(h));
  h = -1;
}

// Environment block: ours plus extras (empty string when there are none: inherit).
std::string env_block(const std::vector<std::pair<std::string, std::string>>& extra) {
  if (extra.empty()) return {};
  std::string block;
  char* cur = GetEnvironmentStringsA();
  for (char* p = cur; *p; p += std::strlen(p) + 1) block.append(p).push_back('\0');
  FreeEnvironmentStringsA(cur);
  for (auto& [k, v] : extra) block.append(k + "=" + v).push_back('\0');
  block.push_back('\0');
  return block;
}

// Start `cmd` with stdin/stdout pipes (stderr to `err`, or merged into stdout) inside a new job object.
struct Started {
  HANDLE process, job, in_write, out_read;
};
Result<Started> start_piped(const std::string& cmd, const fs::path& cwd, const std::string& env, bool merge_stderr) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE in_r, in_w, out_r, out_w;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return fail("CreatePipe failed", int(GetLastError()));
  if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
    CloseHandle(in_r), CloseHandle(in_w);
    return fail("CreatePipe failed", int(GetLastError()));
  }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);  // our ends stay private
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  HANDLE err = merge_stderr ? out_w
                            : CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
  STARTUPINFOA si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = err;
  PROCESS_INFORMATION pi{};
  std::string line = cmd, dir = cwd.string();
  HANDLE job = CreateJobObjectA(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;  // if shaman dies, so do they
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
  std::string env_copy = env;
  BOOL ok = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                           env_copy.empty() ? nullptr : env_copy.data(), dir.empty() ? nullptr : dir.c_str(), &si, &pi);
  CloseHandle(in_r), CloseHandle(out_w);
  if (!merge_stderr) CloseHandle(err);
  if (!ok) {
    CloseHandle(in_w), CloseHandle(out_r), CloseHandle(job);
    return fail("CreateProcess failed: " + cmd, int(GetLastError()));
  }
  AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  return Started{pi.hProcess, job, in_w, out_r};
}

// Read whatever is available without blocking. False once the pipe is closed.
bool read_available(HANDLE h, std::string& into) {
  DWORD avail = 0;
  if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
  while (avail > 0) {
    char buf[65536];
    DWORD n = 0;
    if (!ReadFile(h, buf, std::min<DWORD>(avail, sizeof buf), &n, nullptr) || n == 0) return false;
    into.append(buf, n);
    avail -= n;
  }
  return true;
}

bool write_all(HANDLE h, std::string_view data) {
  size_t off = 0;
  while (off < data.size()) {
    DWORD n = 0;
    if (!WriteFile(h, data.data() + off, DWORD(data.size() - off), &n, nullptr) || n == 0) return false;
    off += n;
  }
  return true;
}
}  // namespace

// ---- long-lived children with piped stdio (MCP and LSP servers) ----
Result<Child> Child::spawn(const std::vector<std::string>& argv,
                           const std::vector<std::pair<std::string, std::string>>& env) {
  if (argv.empty()) return fail("empty command");
  auto cmd = argv;
  // npx, npm, pnpm and friends are .cmd scripts: CreateProcess needs cmd.exe for those.
  if (auto exe = which(cmd[0]); exe && (exe->extension() == ".cmd" || exe->extension() == ".bat"))
    cmd.insert(cmd.begin(), {"cmd.exe", "/C"});
  auto s = start_piped(command_line(cmd), {}, env_block(env), false);
  if (!s) return std::unexpected(s.error());
  Child c;
  c.pid_ = N(s->process);
  c.job_ = N(s->job);
  c.in_ = N(s->in_write);
  c.out_ = N(s->out_read);
  return c;
}

Child::Child(Child&& o) noexcept { *this = std::move(o); }
Child& Child::operator=(Child&& o) noexcept {
  std::swap(pid_, o.pid_), std::swap(in_, o.in_), std::swap(out_, o.out_), std::swap(job_, o.job_), std::swap(buf_, o.buf_);
  return *this;
}
Child::~Child() { kill(); }

Result<void> Child::write_line(const std::string& line) { return write(line + "\n"); }

Result<void> Child::write(std::string_view data) {
  if (!valid(in_) || !write_all(H(in_), data)) return fail("write to child failed");
  return {};
}

Result<std::optional<std::string>> Child::read_line(milliseconds timeout) {
  auto deadline = steady_clock::now() + timeout;
  while (true) {
    if (auto nl = buf_.find('\n'); nl != std::string::npos) {
      std::string line = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return line;
    }
    size_t before = buf_.size();
    if (!read_available(H(out_), buf_) && buf_.size() == before) return fail("child closed its output");
    if (buf_.size() != before) continue;
    if (steady_clock::now() >= deadline) return std::optional<std::string>{};
    Sleep(5);
  }
}

Result<std::optional<std::string>> Child::read_exact(size_t n, milliseconds timeout) {
  auto deadline = steady_clock::now() + timeout;
  while (buf_.size() < n) {
    size_t before = buf_.size();
    if (!read_available(H(out_), buf_) && buf_.size() == before) return fail("child closed its output");
    if (buf_.size() != before) continue;
    if (steady_clock::now() >= deadline) return std::optional<std::string>{};
    Sleep(5);
  }
  std::string out = buf_.substr(0, n);
  buf_.erase(0, n);
  return out;
}

void Child::kill() {
  close_handle(in_);
  close_handle(out_);
  if (valid(job_)) TerminateJobObject(H(job_), 1);
  if (valid(pid_)) WaitForSingleObject(H(pid_), 2000);
  close_handle(pid_);
  close_handle(job_);
}

// ---- background shell jobs (bash tool with background=true) ----
Result<std::shared_ptr<Background>> Background::start(const std::string& command, const fs::path& cwd) {
  std::string line = which("pwsh") ? command_line({"pwsh", "-NoProfile", "-Command", command})
                                   : "cmd.exe /C " + command;
  auto s = start_piped(line, cwd, {}, true);
  if (!s) return std::unexpected(s.error());
  auto bg = std::shared_ptr<Background>(new Background());
  bg->pid_ = N(s->process);
  bg->job_ = N(s->job);
  bg->in_ = N(s->in_write);
  bg->fd_ = N(s->out_read);
  bg->command_ = command;
  return bg;
}

Background::~Background() { kill(); }

void Background::pump() {
  if (!valid(fd_)) return;
  if (!read_available(H(fd_), buf_)) close_handle(fd_);
  if (buf_.size() > (4u << 20)) buf_.erase(0, buf_.size() - (2u << 20));  // keep the tail of chatty processes
}

std::string Background::take_output() {
  std::lock_guard lock(mu_);
  pump();
  std::string out;
  out.swap(buf_);
  return out;
}

bool Background::running() {
  std::lock_guard lock(mu_);
  if (!valid(pid_)) return false;
  if (WaitForSingleObject(H(pid_), 0) != WAIT_OBJECT_0) return true;
  DWORD code = 0;
  GetExitCodeProcess(H(pid_), &code);
  exit_code_ = int(code);
  pump();
  close_handle(pid_);
  return false;
}

Result<void> Background::write_input(const std::string& data) {
  std::lock_guard lock(mu_);
  if (!valid(in_) || !write_all(H(in_), data)) return fail("job is not reading input (it may have exited)");
  return {};
}

void Background::kill() {
  std::lock_guard lock(mu_);
  close_handle(in_);
  if (valid(job_)) TerminateJobObject(H(job_), 1);
  if (valid(pid_)) {
    WaitForSingleObject(H(pid_), 2000);
    exit_code_ = 1;
  }
  close_handle(pid_);
  close_handle(job_);
  close_handle(fd_);
}

}  // namespace shaman::process
