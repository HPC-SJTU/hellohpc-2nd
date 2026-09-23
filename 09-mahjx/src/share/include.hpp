#pragma once

#include <assert.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

#define ENABLE_ENUM_OPERATORS(T)                                                  \
  inline T operator+(const T lhs, const T rhs) { return T(int(lhs) + int(rhs)); } \
  inline T operator-(const T lhs, const T rhs) { return T(int(lhs) - int(rhs)); } \
  inline T operator*(const int lhs, const T rhs) { return T(lhs * int(rhs)); }    \
  inline T operator*(const T lhs, const int rhs) { return T(int(lhs) * rhs); }    \
  inline T operator-(const T lhs) { return T(-int(lhs)); }                        \
  inline T &operator+=(T &lhs, const T rhs) { return lhs = lhs + rhs; }           \
  inline T &operator-=(T &lhs, const T rhs) { return lhs = lhs - rhs; }           \
  inline T &operator*=(T &lhs, const int rhs) { return lhs = T(int(lhs) * rhs); } \
  inline T &operator++(T &lhs) { return lhs = T(int(lhs) + 1); }                  \
  inline T &operator--(T &lhs) { return lhs = T(int(lhs) - 1); }                  \
  inline T operator/(const T lhs, const int rhs) { return T(int(lhs) / rhs); }    \
  inline int operator/(const T lhs, const T rhs) { return int(lhs) / int(rhs); }  \
  inline T &operator/=(T &lhs, const int rhs) { return lhs = T(int(lhs) / rhs); }

void assert_with_out(const bool true_condition, const std::string &comment);
bool str_starts_with(const std::string &str, const std::string &pre);
std::vector<std::string> str_split(const std::string &str, const char del);
bool check_openable_file(const std::string &file_name);
std::vector<std::string> get_files_path(const std::string &dir_name);
void make_dir(const std::string &dir_name);
