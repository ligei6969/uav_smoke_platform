#ifndef SMOKE_CORE_HPP
#define SMOKE_CORE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

#include "smoke/journal.hpp"
#include "smoke/packets.hpp"
#include "smoke/protocol.hpp"

namespace smoke {

struct Request
{
    // Set for client-initiated disconnect notification.
    bool closing = false;
    std::uint64_t client_id = 0;
    std::uint64_t request_id = 0;
    std::string line;
};

struct Response
{
    std::uint64_t client_id = 0;
    std::uint64_t request_id = 0;
    std::string json;
};

struct Stats
{
    std::uint64_t accepted = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t stale = 0;
    std::uint64_t busy = 0;
    std::uint64_t invalid = 0;
    std::uint64_t pings = 0;
    std::uint64_t queue_dropped = 0;
};

// Worker pool consuming a bounded request queue and producing responses.
// When the queue is full, Enqueue reports false so the acceptor replies
// "busy" instead of letting memory grow without bound.
class Gateway
{
public:
    using EmitFn = std::function<void(const Response&)>;

    Gateway(std::size_t workers, std::size_t queue_capacity);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    bool start(Journal* journal, EmitFn emit);
    void stop();

    // Returns false when the queue is full (caller replies "busy").
    bool enqueue(Request&& request);

    Stats stats() const;

    bool journal_loaded() const { return journal_ != nullptr; }

private:
    void worker_loop(std::size_t index);
    void handle(const Request& request);

    Journal* journal_ = nullptr;
    EmitFn emit_;

    std::mutex queue_mutex_;
    std::condition_variable queue_not_empty_;
    std::condition_variable queue_not_full_;
    std::deque<Request> queue_;
    std::size_t queue_capacity_ = 0;
    bool stopping_ = false;

    std::vector<std::thread> workers_;
    // Guarded by queue_mutex_; maps source -> last accepted timestamp_ms.
    std::unordered_map<std::string, std::int64_t> last_timestamp_by_source_;
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}  // namespace smoke

#endif
