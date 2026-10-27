#include "fsutil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace rawr {

bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec) && !ec;
}

bool path_is_dir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec) && !ec;
}

bool path_is_file(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec) && !ec;
}

uint64_t path_size(const std::string& path) {
    std::error_code ec;
    auto n = fs::file_size(fs::path(path), ec);
    if (ec) return 0;
    return static_cast<uint64_t>(n);
}

bool read_file(const std::string& path, Bytes& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0) {
        err = "cannot size " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in && !in.eof()) {
        err = "cannot read " + path;
        return false;
    }
    return true;
}

bool write_file_atomic(const std::string& path, const uint8_t* data, size_t len, std::string& err) {
    std::string tmp = path + ".rawrtmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot create " + tmp;
            return false;
        }
        if (len) out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
        out.flush();
        if (!out) {
            err = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Fall back to copy + remove for filesystems that refuse the rename.
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) {
            err = "cannot replace " + path;
            return false;
        }
    }
    return true;
}

bool write_file_atomic(const std::string& path, const Bytes& data, std::string& err) {
    return write_file_atomic(path, data.data(), data.size(), err);
}

bool copy_file(const std::string& from, const std::string& to, std::string& err) {
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "cannot copy " + from + " to " + to + ": " + ec.message();
        return false;
    }
    return true;
}

bool remove_path(const std::string& path, std::string& err) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        err = "cannot remove " + path + ": " + ec.message();
        return false;
    }
    return true;
}

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return out;
    for (const auto& entry : it) out.push_back(entry.path().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (b[0] == '/' || b[0] == '\\') return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string path_basename(const std::string& path) {
    fs::path p(path);
    return p.filename().string();
}

std::string path_dirname(const std::string& path) {
    fs::path p(path);
    std::string parent = p.parent_path().string();
    return parent.empty() ? std::string(".") : parent;
}

std::string path_extension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return ext;
}

std::string path_stem(const std::string& path) {
    fs::path p(path);
    return p.stem().string();
}

std::string path_absolute(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    if (ec) return path;
    return p.lexically_normal().string();
}

} // namespace rawr
