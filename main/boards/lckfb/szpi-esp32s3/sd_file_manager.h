#pragma once
#include <dirent.h>
#include <algorithm>
#include <vector>
#include "wifi_upload_file.h"

namespace LeleUpload {
constexpr size_t kMaxPathBytes = 232;  // /sdcard/ plus path stays within 240 bytes.
constexpr size_t kMaxQueryBytes = kMaxPathBytes * 6 + 100;
constexpr size_t kPageEntries = 32;
constexpr size_t kDirectoryBudget = 16384;

inline bool ProtectedName(std::string_view name) {
    std::string upper(name);
    for (char& c : upper)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    return upper == "SYSTEM VOLUME INFORMATION" || upper == "$RECYCLE.BIN";
}

inline bool ValidPath(std::string_view path) {
    if (path.size() > kMaxPathBytes ||
        (!path.empty() && (path.front() == '/' || path.back() == '/')))
        return false;
    size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto name = path.substr(start, end == path.npos ? path.npos : end - start);
        if (!ValidName(name) || ProtectedName(name) || name.find('~') != name.npos)
            return false;
        if (end == path.npos)
            break;
        start = end + 1;
    }
    return true;
}

inline int PathStat(const char* path, struct stat* info) {
#ifdef ESP_PLATFORM
    // This endpoint owns the FAT SD mount, which cannot contain symbolic links.
    return ::stat(path, info);
#else
    return ::lstat(path, info);
#endif
}

inline bool DecodeValue(std::string_view text, std::string& value) {
    value.clear();
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '%') {
            if (i + 2 >= text.size() || hex(text[i + 1]) < 0 || hex(text[i + 2]) < 0)
                return false;
            c = char((hex(text[i + 1]) << 4) | hex(text[i + 2]));
            i += 2;
        } else if (c == '+') {
            c = ' ';
        }
        value.push_back(c);
        if (value.size() > kMaxPathBytes)
            return false;
    }
    return true;
}

struct Query {
    std::string path, to, op, name;
    size_t offset = 0;
    bool has_path = false;
    bool replace = false;

    bool Parse(std::string_view text) {
        *this = Query{};
        if (text.size() > kMaxQueryBytes)
            return false;
        unsigned seen = 0;
        while (!text.empty()) {
            const auto end = text.find('&');
            const auto pair = text.substr(0, end);
            const auto equals = pair.find('=');
            if (equals == pair.npos)
                return false;
            const auto key = pair.substr(0, equals);
            const unsigned bit = key == "path"      ? 1
                                 : key == "to"      ? 2
                                 : key == "op"      ? 4
                                 : key == "name"    ? 8
                                 : key == "offset"  ? 16
                                 : key == "replace" ? 32
                                                    : 0;
            if (!bit || (seen & bit))
                return false;
            seen |= bit;
            std::string value;
            if (!DecodeValue(pair.substr(equals + 1), value))
                return false;
            if (key == "path") {
                path = std::move(value);
                has_path = true;
            } else if (key == "to")
                to = std::move(value);
            else if (key == "op")
                op = std::move(value);
            else if (key == "name")
                name = std::move(value);
            else if (key == "replace") {
                if (value != "0" && value != "1")
                    return false;
                replace = value == "1";
            } else {
                if (value.empty() || value.size() > 5)
                    return false;
                for (char c : value) {
                    if (c < '0' || c > '9')
                        return false;
                    offset = offset * 10 + c - '0';
                }
                if (offset > kDirectoryBudget)
                    return false;
            }
            if (end == text.npos)
                break;
            text.remove_prefix(end + 1);
            if (text.empty())
                return false;
        }
        return true;
    }
};

struct Entry {
    std::string name;
    bool directory;
    uint64_t bytes;
};
struct Page {
    std::vector<Entry> entries;
    size_t next = 0;
    bool more = false;
    bool limited = false;
};

class Manager {
public:
    explicit Manager(std::string root) : root_(std::move(root)) {}

    Result Resolve(const std::string& relative, std::string& absolute,
                   bool allow_missing = false) const {
        if (!ValidPath(relative))
            return Result::Invalid;
        absolute = root_;
        struct stat info;
        if (PathStat(absolute.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            return Result::Io;
        size_t start = 0;
        while (start < relative.size()) {
            const auto end = relative.find('/', start);
            const bool last = end == std::string::npos;
            absolute += "/" + relative.substr(start, last ? std::string::npos : end - start);
            if (PathStat(absolute.c_str(), &info) != 0)
                return last && allow_missing && errno == ENOENT ? Result::Ok
                       : errno == ENOENT                        ? Result::NotFound
                                                                : Result::Io;
            if (!S_ISDIR(info.st_mode) && (!last || !S_ISREG(info.st_mode)))
                return Result::Invalid;
            if (last)
                break;
            start = end + 1;
        }
        return Result::Ok;
    }

    Result Info(const std::string& relative, Entry& entry) const {
        std::string absolute;
        const auto result = Resolve(relative, absolute);
        if (result != Result::Ok)
            return result;
        struct stat info;
        if (PathStat(absolute.c_str(), &info) != 0)
            return Result::Io;
        entry = {relative.substr(relative.find_last_of('/') + 1), S_ISDIR(info.st_mode),
                 info.st_size < 0 ? 0 : uint64_t(info.st_size)};
        return Result::Ok;
    }

    Result List(const std::string& relative, size_t offset, Page& page) const {
        page = {};
        if (offset > kDirectoryBudget)
            return Result::Invalid;
        std::string absolute;
        auto result = Resolve(relative, absolute);
        if (result != Result::Ok)
            return result;
        DIR* dir = ::opendir(absolute.c_str());
        if (!dir)
            return Result::Io;
        size_t cursor = 0;
        errno = 0;
        while (cursor < offset && ::readdir(dir))
            ++cursor;
        if (errno) {
            ::closedir(dir);
            return Result::Io;
        }
        unsigned budget = 512;
        bool io_failed = false;
        errno = 0;
        while (cursor < kDirectoryBudget && budget-- && page.entries.size() < kPageEntries) {
            auto* item = ::readdir(dir);
            if (!item)
                break;
            ++cursor;
            const std::string name(item->d_name);
            const auto path = relative.empty() ? name : relative + "/" + name;
            if (!ValidPath(path))
                continue;  // Includes hidden caches and recovery files.
            Entry entry;
            const auto found = Info(path, entry);
            if (found == Result::Ok)
                page.entries.push_back(std::move(entry));
            else if (found != Result::Invalid) {
                io_failed = true;
                break;
            }
            errno = 0;
        }
        const bool failed = errno != 0;
        errno = 0;
        const bool more = ::readdir(dir) != nullptr;
        const bool peek_failed = errno != 0;
        ::closedir(dir);
        if (failed || peek_failed || io_failed)
            return Result::Io;
        page.limited = more && cursor >= kDirectoryBudget;
        page.more = more && !page.limited;
        page.next = cursor;
        return Result::Ok;
    }

    Result Mkdir(const std::string& relative) const {
        if (relative.empty())
            return Result::Invalid;
        std::string absolute;
        const auto result = Resolve(relative, absolute, true);
        if (result != Result::Ok)
            return result;
        if (::mkdir(absolute.c_str(), 0755) == 0)
            return Result::Ok;
        return errno == EEXIST ? Result::Exists : Result::Io;
    }
    Result Move(const std::string& from, const std::string& to) const {
        if (from.empty() || to.empty() || to == from || to.find(from + "/") == 0)
            return Result::Invalid;
        std::string source, destination;
        auto result = Resolve(from, source);
        if (result != Result::Ok)
            return result;
        result = Resolve(to, destination, true);
        if (result != Result::Ok)
            return result;
        struct stat info;
        if (PathStat(destination.c_str(), &info) == 0)
            return Result::Exists;
        if (errno != ENOENT)
            return Result::Io;
        return ::rename(source.c_str(), destination.c_str()) == 0 ? Result::Ok : Result::Io;
    }
    Result Remove(const std::string& relative) const {
        if (relative.empty())
            return Result::Invalid;
        std::string absolute;
        const auto result = Resolve(relative, absolute);
        if (result != Result::Ok)
            return result;
        struct stat info;
        if (PathStat(absolute.c_str(), &info) != 0)
            return Result::Io;
        const int removed =
            S_ISDIR(info.st_mode) ? ::rmdir(absolute.c_str()) : ::unlink(absolute.c_str());
        if (removed == 0)
            return Result::Ok;
        return errno == ENOTEMPTY || errno == EEXIST ? Result::NotEmpty : Result::Io;
    }

private:
    std::string root_;
};
}  // namespace LeleUpload
