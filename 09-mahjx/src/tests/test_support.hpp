#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

namespace test_support {

inline void fail(const std::string &message) {
  std::cerr << message << std::endl;
  std::exit(1);
}

inline void expect(const char *name, const bool condition) {
  if (!condition) fail(name);
}

}  // namespace test_support
