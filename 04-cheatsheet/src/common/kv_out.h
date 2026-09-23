// All harnesses and tools output one "key: value" item per line.
#pragma once
#include <cstdio>
#include <string>

namespace kv {

inline void out(const char* key, const char* value) {
  std::printf("%s: %s\n", key, value);
}
inline void out(const char* key, const std::string& value) {
  std::printf("%s: %s\n", key, value.c_str());
}

inline void out(const char* key, double value, int prec = 6) {
  std::printf("%s: %.*g\n", key, prec, value);
}

inline void out_fixed(const char* key, double value, int decimals) {
  std::printf("%s: %.*f\n", key, decimals, value);
}

inline void out_pct(const char* key, double ratio, int decimals = 1) {
  std::printf("%s: %.*f%%\n", key, decimals, ratio * 100.0);
}

inline void out(const char* key, long value) {
  std::printf("%s: %ld\n", key, value);
}
inline void out(const char* key, int value) {
  std::printf("%s: %d\n", key, value);
}

inline void out_na(const char* key) {
  std::printf("%s: n/a\n", key);
}

inline void flush() { std::fflush(stdout); }

}  // namespace kv
