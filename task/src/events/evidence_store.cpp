#include "smoke/evidence_store.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace smoke {

EvidenceStore::EvidenceStore(const std::string &path, std::uint64_t max_bytes)
    : path_(path), max_bytes_(max_bytes)
{
#ifdef _WIN32
    const int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) {
        failed_ = true;
        return;
    }
#ifdef _WIN32
    file_ = _fdopen(fd, "wb");
    if (!file_) { _close(fd); failed_ = true; }
#else
    file_ = fdopen(fd, "wb");
    if (!file_) { ::close(fd); failed_ = true; }
#endif
    marker_path_ = path_ + ".complete";
}

EvidenceStore::~EvidenceStore()
{
    if (file_) { std::fclose(file_); }
}

bool EvidenceStore::Append(const std::string &record)
{
    if (failed_ || completed_) { return false; }
    // One extra byte covers the record's newline inside the same quota.
    if (bytes_ + record.size() + 1 > max_bytes_) {
        failed_ = true;
        return false;
    }
    if (std::fwrite(record.data(), 1, record.size(), file_) != record.size() ||
        std::fputc('\n', file_) == EOF) {
        failed_ = true;
        return false;
    }
    bytes_ += record.size() + 1;
    ++records_;
    return true;
}

bool EvidenceStore::Complete()
{
    if (failed_ || completed_ || !file_) { return false; }
    if (std::fflush(file_) != 0) {
        failed_ = true;
        return false;
    }
#ifdef _WIN32
    const int fd = _open(marker_path_.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(marker_path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) {
        failed_ = true;
        return false;
    }
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
    std::fclose(file_);
    file_ = nullptr;
    completed_ = true;
    return true;
}

} // namespace smoke
