// Syntax-harness stub: replaces dusk/logging.h so the module can be compiled without fmt.
#pragma once
#include <cstdio>
namespace aurora {
struct Module {
  const char* name;
  explicit Module(const char* n) noexcept : name(n) {}
  template <typename... T> void debug(const char*, T&&...) noexcept {}
  template <typename... T> void info(const char*, T&&...) noexcept {}
  template <typename... T> void warn(const char*, T&&...) noexcept {}
  template <typename... T> void error(const char*, T&&...) noexcept {}
};
}  // namespace aurora
