#include "include.hpp"

#include <dirent.h>

void assert_with_out(const bool true_condition, const std::string &comment) {
  if (!true_condition) {
    std::cout << comment << std::endl;
    assert(false);
  }
}

bool str_starts_with(const std::string &str, const std::string &pre) {
  return str.size() >= pre.size() && std::equal(std::begin(pre), std::end(pre), std::begin(str));
}

std::vector<std::string> str_split(const std::string &str, const char del) {
  int first = 0;
  int last = str.find_first_of(del);
  std::vector<std::string> result;
  while (first < str.size()) {
    std::string subStr(str, first, last - first);
    result.push_back(subStr);
    first = last + 1;
    last = str.find_first_of(del, first);
    if (last == std::string::npos) {
      last = str.size();
    }
  }
  return result;
}

bool check_openable_file(const std::string &file_name) {
  std::ifstream ifs(file_name);
  return ifs.is_open();
}

std::vector<std::string> get_files_path(const std::string &dir_name) {
  std::vector<std::string> ret;
  DIR *dir = opendir(dir_name.c_str());
  assert_with_out(dir != nullptr, "get_files_path error: cannot open directory:" + dir_name);
  if (dir == nullptr) {
    return ret;
  }
  while (struct dirent *entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    ret.push_back(dir_name + "/" + name);
  }
  closedir(dir);
  std::sort(ret.begin(), ret.end());  // Deterministic order, as with ls -1.
  return ret;
}

void make_dir(const std::string &dir_name) {
  // Create the directory and all missing parent directories.
  std::string current;
  size_t pos = 0;
  while (true) {
    pos = dir_name.find('/', pos);
    const std::string part = dir_name.substr(0, pos == std::string::npos ? dir_name.size() : pos);
    if (!part.empty()) {
      mkdir(part.c_str(), 0755);  // Ignore EEXIST when the directory exists.
    }
    if (pos == std::string::npos) {
      break;
    }
    pos += 1;
  }
}
