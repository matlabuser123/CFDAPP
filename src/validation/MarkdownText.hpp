#pragma once

#include <string>

// Private to src/validation: the one text normalisation every validation
// Markdown report writer applies to its output, so the committed reports
// pass `git diff --check` -- no trailing spaces/tabs on any line (a check
// detail with an empty tail, a "; "-joined list) and exactly one newline
// at the end of the file (no trailing blank line). Content is unchanged.
namespace cfd::validation::detail {

inline std::string tidyMarkdown(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  std::string line;
  for (const char c : text) {
    if (c == '\n') {
      const auto end = line.find_last_not_of(" \t");
      out.append(line, 0, end == std::string::npos ? 0 : end + 1);
      out.push_back('\n');
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  const auto end = line.find_last_not_of(" \t");
  out.append(line, 0, end == std::string::npos ? 0 : end + 1);
  while (!out.empty() && out.back() == '\n') out.pop_back();
  out.push_back('\n');
  return out;
}

}  // namespace cfd::validation::detail
