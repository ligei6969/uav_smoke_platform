#ifndef SMOKE_JOURNAL_HPP
#define SMOKE_JOURNAL_HPP

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>

namespace smoke {

// Append-only durable journal of accepted event keys. The sole-owner lock is
// implemented portably: an exclusive-create lock file next to the journal.
class Journal
{
public:
    Journal() = default;
    ~Journal();

    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    // Takes the single-owner lock (fails when another process owns it), then
    // replays existing entries into the dedupe set. Returns false when the
    // journal is owned by another live process.
    bool open(const std::string& journal_path);

    // Durably record an accepted key before its ACK is sent. Atomically
    // checks-and-records: returns false when the key was already present,
    // in which case nothing is appended.
    bool append_if_absent(const std::string& key);

    bool seen(const std::string& key) const;

    std::uint64_t size() const { return count_; }

    void close();

private:
    bool append(const std::string& key);
    bool owner_is_dead() const;

    std::ofstream out_;
    std::mutex mutex_;
    std::string lock_path_;
    std::unordered_set<std::string> keys_;
    std::uint64_t count_ = 0;
};

}  // namespace smoke

#endif
