#include "smoke/core.hpp"

namespace smoke {

Gateway::Gateway(std::size_t workers, std::size_t queue_capacity)
    : queue_capacity_(queue_capacity)
{
    if (workers == 0)
    {
        workers = 1;
    }
    workers_.reserve(workers);
}

Gateway::~Gateway()
{
    stop();
}

bool Gateway::start(Journal* journal, EmitFn emit)
{
    journal_ = journal;
    emit_ = std::move(emit);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = false;
    }
    const std::size_t count = workers_.capacity();
    for (std::size_t i = 0; i < count; ++i)
    {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
    return true;
}

void Gateway::stop()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
    }
    queue_not_empty_.notify_all();
    for (std::thread& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers_.clear();
}

bool Gateway::enqueue(Request&& request)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stopping_)
        {
            return false;
        }
        if (queue_.size() >= queue_capacity_)
        {
            ++stats_.queue_dropped;
            return false;
        }
        queue_.push_back(std::move(request));
    }
    queue_not_empty_.notify_one();
    return true;
}

Stats Gateway::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void Gateway::worker_loop(std::size_t /*index*/)
{
    while (true)
    {
        Request request;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_not_empty_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
            {
                return;
            }
            request = std::move(queue_.front());
            queue_.pop_front();
        }
        queue_not_full_.notify_all();
        handle(request);
    }
}

void Gateway::handle(const Request& request)
{
    Response response;
    response.client_id = request.client_id;
    response.request_id = request.request_id;

    if (request.line == "PING")
    {
        response.json = "{\"status\":\"ok\"}";
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.pings;
        }
        emit_(response);
        return;
    }
    if (request.line == "STATS")
    {
        const Stats snapshot = stats();
        std::lock_guard<std::mutex> lock(queue_mutex_);
        response.json =
            "{\"status\":\"ok\",\"accepted\":" + std::to_string(snapshot.accepted) +
            ",\"duplicates\":" + std::to_string(snapshot.duplicates) +
            ",\"stale\":" + std::to_string(snapshot.stale) +
            ",\"busy\":" + std::to_string(snapshot.busy) +
            ",\"invalid\":" + std::to_string(snapshot.invalid) +
            ",\"pings\":" + std::to_string(snapshot.pings) +
            ",\"queue_dropped\":" + std::to_string(snapshot.queue_dropped) +
            ",\"journal_entries\":" + std::to_string(journal_ ? journal_->size() : 0) +
            ",\"queue_depth\":" + std::to_string(queue_.size()) + "}";
        emit_(response);
        return;
    }

    DetectEvent event;
    if (!parse_detect(request.line, event))
    {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.invalid;
        }
        response.json = "{\"status\":\"invalid\"}";
        emit_(response);
        return;
    }

    // Timestamps must be non-decreasing per source; older frames are stale.
    const std::string key = event_key(event);
    bool stale = false;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        std::int64_t& last = last_timestamp_by_source_[event.source];
        if (event.timestamp_ms < last)
        {
            stale = true;
        }
        else
        {
            last = event.timestamp_ms;
        }
    }
    if (stale)
    {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.stale;
        }
        response.json = "{\"status\":\"stale\"}";
        emit_(response);
        return;
    }

    // Durable write first; only then ACK. append_if_absent makes check and
    // append one atomic step, so concurrent retries cannot both be accepted.
    // A crash after the append but before the ACK replays as "duplicate"
    // instead of losing the event.
    if (!journal_->append_if_absent(key))
    {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.duplicates;
        }
        response.json = "{\"status\":\"duplicate\"}";
        emit_(response);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.accepted;
    }
    response.json = "{\"status\":\"accepted\",\"key\":\"" + key + "\"}";
    emit_(response);
}

}  // namespace smoke
