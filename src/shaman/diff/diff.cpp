#include "shaman/diff/diff.hpp"

#include <algorithm>
#include <format>
#include <vector>

#include "shaman/core/strings.hpp"

namespace shaman::diff {
namespace {

enum class Op { keep, del, ins };
struct Edit {
  Op op;
  size_t a, b;  // line indices in before / after
};

// Myers O(ND) shortest edit script with a linear trace of V arrays.
std::vector<Edit> myers(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  const int n = int(a.size()), m = int(b.size()), max = n + m;
  std::vector<int> v(2 * max + 2, 0);
  std::vector<std::vector<int>> trace;
  int d_found = -1;
  for (int d = 0; d <= max && d_found < 0; ++d) {
    trace.push_back(v);
    for (int k = -d; k <= d; k += 2) {
      int x = (k == -d || (k != d && v[k - 1 + max] < v[k + 1 + max])) ? v[k + 1 + max] : v[k - 1 + max] + 1;
      int y = x - k;
      while (x < n && y < m && a[x] == b[y]) ++x, ++y;
      v[k + max] = x;
      if (x >= n && y >= m) {
        d_found = d;
        break;
      }
    }
  }
  std::vector<Edit> out;
  int x = n, y = m;
  for (int d = d_found; d > 0; --d) {
    auto& vd = trace[d];
    int k = x - y;
    int prev_k = (k == -d || (k != d && vd[k - 1 + max] < vd[k + 1 + max])) ? k + 1 : k - 1;
    int prev_x = vd[prev_k + max], prev_y = prev_x - prev_k;
    while (x > prev_x && y > prev_y) out.push_back({Op::keep, size_t(--x), size_t(--y)});
    if (x == prev_x) out.push_back({Op::ins, size_t(x), size_t(--y)});
    else out.push_back({Op::del, size_t(--x), size_t(y)});
  }
  while (x > 0 && y > 0) out.push_back({Op::keep, size_t(--x), size_t(--y)});
  std::reverse(out.begin(), out.end());
  return out;
}

}  // namespace

std::string unified(const std::string& before, const std::string& after, const std::string& path, int context) {
  if (before == after) return "";
  auto a = str::lines(before), b = str::lines(after);
  if (a.size() + b.size() > 40'000) return std::format("--- a/{}\n+++ b/{}\n(file too large to diff: {} -> {} lines)\n", path, path, a.size(), b.size());
  auto edits = myers(a, b);
  std::string out = std::format("--- a/{}\n+++ b/{}\n", path, path);
  size_t i = 0;
  while (i < edits.size()) {
    if (edits[i].op == Op::keep) {
      ++i;
      continue;
    }
    // Hunk: back up `context` keeps, then extend while changes are within 2*context of each other.
    size_t start = i >= size_t(context) ? i - context : 0;
    while (start < i && edits[start].op != Op::keep) ++start;
    size_t end = i;
    size_t last_change = i;
    while (end < edits.size()) {
      if (edits[end].op != Op::keep) last_change = end;
      else if (end - last_change > size_t(2 * context)) break;
      ++end;
    }
    end = std::min(edits.size(), last_change + context + 1);
    size_t a_start = edits[start].a, b_start = edits[start].b, a_len = 0, b_len = 0;
    std::string body;
    for (size_t j = start; j < end; ++j) {
      auto& e = edits[j];
      if (e.op == Op::keep) body += " " + a[e.a] + "\n", ++a_len, ++b_len;
      else if (e.op == Op::del) body += "-" + a[e.a] + "\n", ++a_len;
      else body += "+" + b[e.b] + "\n", ++b_len;
    }
    out += std::format("@@ -{},{} +{},{} @@\n", a_len ? a_start + 1 : a_start, a_len, b_len ? b_start + 1 : b_start, b_len) + body;
    i = end;
  }
  return out;
}

Stats stats(const std::string& d) {
  Stats s;
  for (auto& l : str::lines(d)) {
    if (l.starts_with("+++") || l.starts_with("---")) continue;
    if (l.starts_with("+")) ++s.added;
    else if (l.starts_with("-")) ++s.removed;
  }
  return s;
}

}  // namespace shaman::diff
