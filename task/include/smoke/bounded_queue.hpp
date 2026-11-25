#ifndef SMOKE_BOUNDED_QUEUE_HPP
#define SMOKE_BOUNDED_QUEUE_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace smoke {

// Latest mode is for independently usable decoded frames/metadata, never H264
// byte chunks. Call Close(), then join all users before destroying the queue.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {
        if (capacity == 0 || capacity > 4096) {
            throw std::invalid_argument("queue capacity must be in [1, 4096]");
        }
    }

    BoundedQueue(const BoundedQueue &) = delete;
    BoundedQueue &operator=(const BoundedQueue &) = delete;

    bool PushLatest(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }
            if (items_.size() == capacity_) {
                items_.pop_front();
                ++dropped_;
            }
            items_.push_back(std::move(value));
            UpdateHighWaterMark();
        }
        not_empty_.notify_one();
        return true;
    }

    bool PushFor(T value, std::chrono::milliseconds timeout)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!not_full_.wait_for(lock, timeout, [this] {
                    return closed_ || items_.size() < capacity_;
                }) || closed_) {
                return false;
            }
            items_.push_back(std::move(value));
            UpdateHighWaterMark();
        }
        not_empty_.notify_one();
        return true;
    }

    bool PopFor(T &value, std::chrono::milliseconds timeout)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!not_empty_.wait_for(lock, timeout, [this] {
                    return closed_ || !items_.empty();
                }) || items_.empty()) {
                return false;
            }
            value = std::move(items_.front());
            items_.pop_front();
        }
        not_full_.notify_one();
        return true;
    }

    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    std::size_t HighWaterMark() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return high_water_mark_;
    }

    std::size_t Dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    void UpdateHighWaterMark()
    {
        if (items_.size() > high_water_mark_) {
            high_water_mark_ = items_.size();
        }
    }

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> items_;
    bool closed_ = false;
    std::size_t high_water_mark_ = 0;
    std::size_t dropped_ = 0;
};

} // namespace smoke

#endif // SMOKE_BOUNDED_QUEUE_HPP
