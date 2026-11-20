#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace LeleUpload {
constexpr size_t kMaxFileBytes = 64 * 1024 * 1024;
constexpr size_t kMaxNameBytes = 180;

inline bool ValidName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameBytes || name.front() == '.' || name.front() == ' ' ||
        name.back() == '.' || name.back() == ' ')
        return false;
    for (size_t i = 0; i < name.size();) {
        const auto c = static_cast<unsigned char>(name[i++]);
        if (c < 0x80) {
            if (c < 32 || c == 127 ||
                std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                return false;
            continue;
        }
        unsigned tail = c >= 0xc2 && c <= 0xdf   ? 1
                        : c >= 0xe0 && c <= 0xef ? 2
                        : c >= 0xf0 && c <= 0xf4 ? 3
                                                 : 0;
        if (!tail || i + tail > name.size())
            return false;
        uint32_t cp = c & ((1u << (6 - tail)) - 1);
        for (unsigned j = 0; j < tail; ++j) {
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < (tail == 1   ? 0x80u
                  : tail == 2 ? 0x800u
                              : 0x10000u) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
            return false;
    }
    auto base = std::string(name.substr(0, name.find('.')));
    for (char& c : base)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    return base != "CON" && base != "PRN" && base != "AUX" && base != "NUL" &&
           !(base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") &&
             base[3] >= '1' && base[3] <= '9');
}

enum class Result { Ok, Invalid, Exists, Io, NotFound, NotEmpty, Recovery };

// Caller owns exclusive SD access. A partial request never exposes a final
// filename; the destructor removes only this transaction's temporary file.
class File {
public:
    ~File() {
        if (file_)
            std::fclose(file_);
        if (!temporary_.empty())
            ::unlink(temporary_.c_str());
    }
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    Result Open(const std::string& directory, std::string_view name, size_t size,
                const std::string& nonce, bool replace = false) {
        if (file_ || !temporary_.empty() || !ValidName(name) || !size || size > kMaxFileBytes ||
            nonce.empty() || nonce.find_first_not_of("0123456789abcdef") != std::string::npos)
            return Result::Invalid;
        struct stat info;
        // ESP-IDF FatFS recognizes the mounted root as "/", not an empty
        // path. Stat with a trailing slash and never mkdir an existing parent.
        const auto parent = directory + "/";
        if (::stat(parent.c_str(), &info) != 0) {
            if (errno != ENOENT || (::mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) ||
                ::stat(parent.c_str(), &info) != 0)
                return Result::Io;
        }
        if (!S_ISDIR(info.st_mode))
            return Result::Io;
        final_ = directory + "/" + std::string(name);
        const bool exists = ::stat(final_.c_str(), &info) == 0;
        if (exists && (!replace || !S_ISREG(info.st_mode)))
            return Result::Exists;
        if (!exists && errno != ENOENT)
            return Result::Io;
        replace_ = replace;
        backup_ = directory + "/.lele-old-" + nonce + "-" + std::string(name);
        const auto temporary = directory + "/.lele-upload-" + nonce + ".part";
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0)
            return Result::Io;
        temporary_ = temporary;
        file_ = ::fdopen(fd, "wb");
        if (!file_) {
            ::close(fd);
            return Result::Io;
        }
        expected_ = size;
        return Result::Ok;
    }
    bool Write(const void* data, size_t size) {
        if (!file_ || size > expected_ - received_)
            return false;
        if (std::fwrite(data, 1, size, file_) != size)
            return false;
        received_ += size;
        return true;
    }
    Result Finish() {
        if (!file_ || received_ != expected_)
            return Result::Invalid;
        bool ok = std::fflush(file_) == 0;
        if (ok)
            ok = ::fsync(::fileno(file_)) == 0;
        if (std::fclose(file_) != 0)
            ok = false;
        file_ = nullptr;
        if (!ok)
            return Result::Io;
        struct stat info;
        const bool exists = ::stat(final_.c_str(), &info) == 0;
        if (exists && (!replace_ || !S_ISREG(info.st_mode)))
            return Result::Exists;
        if (!exists && errno != ENOENT)
            return Result::Io;
        if (exists) {
            // FAT does not support POSIX replacement rename. Preserve the old
            // file until the complete new file has reached its final name.
            if (::stat(backup_.c_str(), &info) == 0 || errno != ENOENT)
                return Result::Io;
            if (::rename(final_.c_str(), backup_.c_str()) != 0)
                return Result::Io;
        }
        if (::rename(temporary_.c_str(), final_.c_str()) != 0) {
            if (exists && (::stat(final_.c_str(), &info) == 0 || errno != ENOENT ||
                           ::rename(backup_.c_str(), final_.c_str()) != 0))
                return Result::Recovery;  // Never delete the retained original.
            return Result::Io;
        }
        temporary_.clear();
        if (exists)
            backup_retained_ = ::unlink(backup_.c_str()) != 0;
        return Result::Ok;
    }
    bool BackupRetained() const { return backup_retained_; }

private:
    FILE* file_ = nullptr;
    std::string final_;
    std::string temporary_;
    std::string backup_;
    bool replace_ = false;
    bool backup_retained_ = false;
    size_t expected_ = 0;
    size_t received_ = 0;
};
}  // namespace LeleUpload
