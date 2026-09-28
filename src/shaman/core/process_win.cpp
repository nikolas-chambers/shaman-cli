// Windows implementation of shaman::process. Untested in CI so far.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
  HANDLE rd, wr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return fail("CreatePipe failed", int(GetLastError()));
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  std::string cmd = command_line(argv);
  std::string cwd = opts.cwd.string();
  // A job object lets us kill the whole process tree on timeout.
  HANDLE job = CreateJobObjectA(nullptr, nullptr);
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                      nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    CloseHandle(rd), CloseHandle(wr), CloseHandle(job);
    return fail("CreateProcess failed: " + cmd, int(GetLastError()));
  }
  AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(wr);

  Output out;
  auto deadline = steady_clock::now() + opts.timeout;
  char buf[8192];
  while (true) {
    if (opts.cancel && opts.cancel->load()) { out.cancelled = true; break; }
    if (steady_clock::now() >= deadline) { out.timed_out = true; break; }
    DWORD avail = 0;
    if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;  // closed
    if (avail == 0) {
      if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
      }
      continue;
    }
    DWORD n = 0;
    if (!ReadFile(rd, buf, sizeof buf, &n, nullptr) || n == 0) break;
    if (out.output.size() < opts.max_output)
      out.output.append(buf, std::min<size_t>(n, opts.max_output - out.output.size()));
  }
  if (out.timed_out || out.cancelled) TerminateJobObject(job, 1);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  out.exit_code = int(code);
  CloseHandle(rd), CloseHandle(pi.hProcess), CloseHandle(pi.hThread), CloseHandle(job);
  return out;
}

Result<Output> shell(const std::string& command, const Options& opts) {
  if (which("pwsh")) return run({"pwsh", "-NoProfile", "-Command", command}, opts);
  return run({"cmd.exe", "/C", command}, opts);
}

// Long-lived children (MCP stdio servers) are not implemented on Windows yet.
Result<Child> Child::spawn(const std::vector<std::string>&,
                           const std::vector<std::pair<std::string, std::string>>&) {
  return fail("stdio child processes are not yet supported on Windows");
}
Child::Child(Child&& o) noexcept { *this = std::move(o); }
Child& Child::operator=(Child&& o) noexcept {
  std::swap(pid_, o.pid_), std::swap(in_, o.in_), std::swap(out_, o.out_), std::swap(buf_, o.buf_);
  return *this;
}
Child::~Child() = default;
Result<void> Child::write_line(const std::string&) { return fail("unsupported"); }
Result<void> Child::write(std::string_view) { return fail("unsupported"); }
Result<std::optional<std::string>> Child::read_line(milliseconds) { return fail("unsupported"); }
Result<std::optional<std::string>> Child::read_exact(size_t, milliseconds) { return fail("unsupported"); }
void Child::kill() {}

}  // namespace shaman::process
