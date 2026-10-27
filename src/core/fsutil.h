// Filesystem helpers shared by the core and the platform layers.
#pragma once

#include <string>
#include <vector>

#include "common.h"

namespace rawr {

bool path_exists(const std::string& path);
bool path_is_dir(const std::string& path);
bool path_is_file(const std::string& path);
uint64_t path_size(const std::string& path);

bool read_file(const std::string& path, Bytes& out, std::string& err);
// Writes through a temporary file and renames it into place, so a crash
// never leaves a half written file behind.
bool write_file_atomic(const std::string& path, const uint8_t* data, size_t len, std::string& err);
bool write_file_atomic(const std::string& path, const Bytes& data, std::string& err);
bool copy_file(const std::string& from, const std::string& to, std::string& err);
bool remove_path(const std::string& path, std::string& err);

std::vector<std::string> list_dir(const std::string& dir);

std::string path_join(const std::string& a, const std::string& b);
std::string path_basename(const std::string& path);
std::string path_dirname(const std::string& path);
std::string path_extension(const std::string& path);   // lower case, without the dot
std::string path_stem(const std::string& path);       // filename without extension
std::string path_absolute(const std::string& path);

} // namespace rawr
