#ifndef SMOKE_EVIDENCE_STORE_HPP
#define SMOKE_EVIDENCE_STORE_HPP

#include <cstdint>
#include <cstdio>
#include <string>

namespace smoke {

// Append-only JSONL evidence writer with an exclusive-create output, a hard
// byte quota, and an atomic completion marker. Quota exhaustion fails the
// store (no silent truncation); the completion marker file is written only
// after the final flush, so consumers may trust "<path>.complete".
// Incomplete evidence must never be reported as a full result.
class EvidenceStore {
public:
    EvidenceStore(const std::string &path, std::uint64_t max_bytes);
    ~EvidenceStore();

    EvidenceStore(const EvidenceStore &) = delete;
    EvidenceStore &operator=(const EvidenceStore &) = delete;

    // Appends one JSONL record plus newline. Returns false once the record
    // would exceed the quota; the store is then failed for good.
    bool Append(const std::string &json_record);

    // Flushes and writes the completion marker. Returns false on I/O failure
    // or if the store has already failed. After success the store is closed.
    bool Complete();

    std::uint64_t Bytes() const { return bytes_; }
    std::uint64_t Records() const { return records_; }
    bool Failed() const { return failed_; }
    bool Completed() const { return completed_; }

private:
    FILE *file_ = nullptr;
    std::string path_;
    std::string marker_path_;
    std::uint64_t max_bytes_ = 0;
    std::uint64_t bytes_ = 0;
    std::uint64_t records_ = 0;
    bool failed_ = false;
    bool completed_ = false;
};

} // namespace smoke
#endif
