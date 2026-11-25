#include "smoke/bounded_queue.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            std::ostringstream message;                                         \
            message << __FILE__ << ':' << __LINE__ << ": " << #expression;      \
            throw std::runtime_error(message.str());                            \
        }                                                                       \
    } while (false)

namespace {

using Milliseconds = std::chrono::milliseconds;

void TestCapacity()
{
    for (std::size_t invalid : {std::size_t(0), std::size_t(4097)}) {
        bool threw = false;
        try {
            smoke::BoundedQueue<int> queue(invalid);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw);
    }
    smoke::BoundedQueue<int> maximum(4096);
    CHECK(maximum.Size() == 0);
    CHECK(maximum.HighWaterMark() == 0);
    CHECK(maximum.Dropped() == 0);
}

void TestLatestDropsOldest()
{
    smoke::BoundedQueue<int> queue(2);
    CHECK(queue.PushLatest(1));
    CHECK(queue.PushLatest(2));
    CHECK(queue.PushLatest(3));
    CHECK(queue.PushLatest(4));
    CHECK(queue.Size() == 2);
    CHECK(queue.HighWaterMark() == 2);
    CHECK(queue.Dropped() == 2);
    int value = 0;
    CHECK(queue.PopFor(value, Milliseconds(0)) && value == 3);
    CHECK(queue.PopFor(value, Milliseconds(0)) && value == 4);
    CHECK(!queue.PopFor(value, Milliseconds(0)));
}

void TestMoveOnly()
{
    smoke::BoundedQueue<std::unique_ptr<int>> queue(1);
    CHECK(queue.PushLatest(std::unique_ptr<int>(new int(7))));
    CHECK(queue.PushLatest(std::unique_ptr<int>(new int(8))));
    std::unique_ptr<int> result;
    CHECK(queue.PopFor(result, Milliseconds(0)));
    CHECK(result && *result == 8);
    CHECK(queue.PushFor(std::unique_ptr<int>(new int(9)), Milliseconds(0)));
    CHECK(queue.PopFor(result, Milliseconds(0)));
    CHECK(result && *result == 9);
    CHECK(queue.Dropped() == 1);
}

void TestTimeouts()
{
    smoke::BoundedQueue<int> queue(1);
    int value = 55;
    auto start = std::chrono::steady_clock::now();
    CHECK(!queue.PopFor(value, Milliseconds(30)));
    CHECK(value == 55);
    CHECK(std::chrono::steady_clock::now() - start >= Milliseconds(20));

    CHECK(queue.PushFor(1, Milliseconds(0)));
    start = std::chrono::steady_clock::now();
    CHECK(!queue.PushFor(2, Milliseconds(30)));
    CHECK(std::chrono::steady_clock::now() - start >= Milliseconds(20));
    CHECK(queue.Dropped() == 0);
    CHECK(queue.PopFor(value, Milliseconds(0)) && value == 1);
}

void TestCloseDrainsAndRejectsWrites()
{
    smoke::BoundedQueue<int> queue(2);
    CHECK(queue.PushLatest(1));
    CHECK(queue.PushLatest(2));
    queue.Close();
    queue.Close();
    CHECK(!queue.PushLatest(3));
    CHECK(!queue.PushFor(3, Milliseconds(5000)));
    int value = 0;
    CHECK(queue.PopFor(value, Milliseconds(5000)) && value == 1);
    CHECK(queue.PopFor(value, Milliseconds(5000)) && value == 2);
    CHECK(!queue.PopFor(value, Milliseconds(5000)));
    CHECK(queue.Size() == 0);
}

void TestCloseWakesConsumer()
{
    smoke::BoundedQueue<int> queue(1);
    auto waiting = std::async(std::launch::async, [&queue] {
        int value = 0;
        return queue.PopFor(value, Milliseconds(5000));
    });
    const auto before_close = waiting.wait_for(Milliseconds(30));
    queue.Close();
    const auto after_close = waiting.wait_for(Milliseconds(500));
    const bool popped = waiting.get();
    CHECK(before_close == std::future_status::timeout);
    CHECK(after_close == std::future_status::ready);
    CHECK(!popped);
}

void TestCloseWakesProducer()
{
    smoke::BoundedQueue<int> queue(1);
    CHECK(queue.PushLatest(1));
    auto waiting = std::async(std::launch::async, [&queue] {
        return queue.PushFor(2, Milliseconds(5000));
    });
    const auto before_close = waiting.wait_for(Milliseconds(30));
    queue.Close();
    const auto after_close = waiting.wait_for(Milliseconds(500));
    const bool pushed = waiting.get();
    CHECK(before_close == std::future_status::timeout);
    CHECK(after_close == std::future_status::ready);
    CHECK(!pushed);
    int value = 0;
    CHECK(queue.PopFor(value, Milliseconds(0)) && value == 1);
}

void TestPopWakesBackpressuredProducer()
{
    smoke::BoundedQueue<int> queue(1);
    CHECK(queue.PushLatest(1));
    auto waiting = std::async(std::launch::async, [&queue] {
        return queue.PushFor(2, Milliseconds(5000));
    });
    const auto before_pop = waiting.wait_for(Milliseconds(30));
    int value = 0;
    const bool popped_first = queue.PopFor(value, Milliseconds(0));
    const int first = value;
    const auto after_pop = waiting.wait_for(Milliseconds(500));
    const bool pushed_second = waiting.get();
    queue.Close();
    CHECK(before_pop == std::future_status::timeout);
    CHECK(popped_first && first == 1);
    CHECK(after_pop == std::future_status::ready);
    CHECK(pushed_second);
    CHECK(queue.PopFor(value, Milliseconds(0)) && value == 2);
}

void TestConcurrentDrainExactlyOnce()
{
    const int producer_count = 4;
    const int per_producer = 500;
    smoke::BoundedQueue<int> queue(7);
    std::vector<int> counts(producer_count * per_producer, 0);
    std::mutex counts_mutex;
    std::atomic<bool> valid(true);
    std::vector<std::thread> consumers;
    for (int consumer = 0; consumer < 3; ++consumer) {
        consumers.emplace_back([&] {
            int value = 0;
            while (queue.PopFor(value, Milliseconds(5000))) {
                std::lock_guard<std::mutex> lock(counts_mutex);
                if (value < 0 || value >= static_cast<int>(counts.size())) {
                    valid.store(false);
                } else {
                    ++counts[static_cast<std::size_t>(value)];
                }
            }
        });
    }
    std::vector<std::thread> producers;
    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            for (int index = 0; index < per_producer; ++index) {
                const int value = producer * per_producer + index;
                while (!queue.PushFor(value, Milliseconds(10))) {
                    // Offline mode retries the same immutable item after timeout.
                }
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }
    queue.Close();
    for (auto &consumer : consumers) {
        consumer.join();
    }
    CHECK(valid.load());
    for (int count : counts) {
        CHECK(count == 1);
    }
    CHECK(queue.Size() == 0);
    CHECK(queue.HighWaterMark() > 0 && queue.HighWaterMark() <= 7);
    CHECK(queue.Dropped() == 0);
}

} // namespace

int main()
{
    try {
        TestCapacity();
        TestLatestDropsOldest();
        TestMoveOnly();
        TestTimeouts();
        TestCloseDrainsAndRejectsWrites();
        TestCloseWakesConsumer();
        TestCloseWakesProducer();
        TestPopWakesBackpressuredProducer();
        TestConcurrentDrainExactlyOnce();
        std::cout << "BoundedQueue: 9 tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
