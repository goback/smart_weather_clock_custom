
#pragma once
#include <map>
#include <string>
struct Entry { size_t size; bool regular; };
struct File {
  const Entry* entry;
  explicit operator bool() const { return entry != nullptr; }
  bool isFile() const { return entry->regular; }
  size_t size() const { return entry->size; }
  void close() {}
};
struct {
  std::map<std::string,Entry> files;
  std::string failRemove;
  bool exists(const char* path) { return files.count(path); }
  File open(const char* path, const char* mode) {
    if (*mode == 'w') files.try_emplace(path, Entry{0,true});
    auto found = files.find(path);
    return {found == files.end() ? nullptr : &found->second};
  }
  bool remove(const char* path) { return failRemove != path && files.erase(path); }
} LittleFS;
