// Test-harness stub for dusk/logging.h.
//
// It PRINTS rather than swallowing, and that is deliberate. The classification report is the
// deliverable of the instrumentation work - one press has to answer every open question - and a
// stub that discarded its output meant sixty lines of format strings could be written, compiled,
// and shipped without anyone ever seeing what they produce. A report nobody can read is the same
// as no report (CLAUDE.md rule 2's corollary), and the only way to know it reads well is to look
// at it.
//
// Uses real fmt so the format strings are checked the same way the game checks them: an argument
// count mismatch or a bad spec is a compile error here, not a surprise in a log file.
#pragma once
#include <fmt/core.h>
#include <cstdio>

namespace aurora {
struct Module {
  const char* name;
  explicit Module(const char* n) noexcept : name(n) {}

  // Set by the harness around the block it wants to see. Off by default so the ordinary
  // behavioural cases stay quiet and their PASS/FAIL lines remain readable.
  static bool& enabled() { static bool e = false; return e; }

  template <typename... T> void info(fmt::format_string<T...> f, T&&... a) const noexcept {
    if (!enabled()) return;
    std::fputs("      ", stdout);
    std::fputs(fmt::format(f, std::forward<T>(a)...).c_str(), stdout);
    std::fputc('\n', stdout);
  }
  template <typename... T> void debug(fmt::format_string<T...>, T&&...) const noexcept {}
  template <typename... T> void warn(fmt::format_string<T...> f, T&&... a) const noexcept {
    info(f, std::forward<T>(a)...);
  }
  template <typename... T> void error(fmt::format_string<T...> f, T&&... a) const noexcept {
    info(f, std::forward<T>(a)...);
  }
};
}  // namespace aurora
