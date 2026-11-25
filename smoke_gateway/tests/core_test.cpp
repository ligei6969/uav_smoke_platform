#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "smoke/core.hpp"
#include "smoke/journal.hpp"
#include "smoke/protocol.hpp"

using namespace smoke;

namespace
{

std::string temp_dir()
{
    static int counter = 0;
    const std::string dir =
        (std::filesystem::temp_directory_path() /
         ("smoke_core_test_" + std::to_string(++counter)))
            .string();
    std::filesystem::create_directories(dir);
    return dir;
}

Response exchange(Gateway& gateway, Journal& journal, const std::string& line)
{
    Response out;
    std::mutex mutex;
    std::condition_variable cv;
    gateway.start(&journal, [&](const Response& response) {
        std::lock_guard<std::mutex> lock(mutex);
        out = response;
        cv.notify_all();
    });
    Request request;
    request.client_id = 1;
    request.line = line;
    const bool enqueued = gateway.enqueue(std::move(request));
    assert(enqueued);
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return !out.json.empty(); });
    }
    gateway.stop();
    return out;
}

}  // namespace

int main()
{
    // Protocol: valid line.
    {
        DetectEvent event;
        assert(parse_detect(
            "DETECT drone-1 102 3367 0.9 10 20 30 40 1920 1080", event));
        assert(event.source == "drone-1");
        assert(event.frame_id == 102);
        assert(event.confidence == 0.9);
        assert(event.frame_width == 1920);
    }
    // Protocol: rejections.
    {
        DetectEvent event;
        assert(!parse_detect("DETECT drone 1 2 nan 0 0 1 1 10 10", event));
        assert(!parse_detect("DETECT drone 1 2 inf 0 0 1 1 10 10", event));
        assert(!parse_detect("DETECT drone 1 2 1.5 0 0 1 1 10 10", event));
        assert(!parse_detect("DETECT a\nb 1 2 0.5 0 0 1 1 10 10", event));
        assert(!parse_detect("DETECT drone -1 2 0.5 0 0 1 1 10 10", event));
        assert(!parse_detect("DETECT drone 1 2 0.5 0 0 1 1 10", event));
        assert(!parse_detect("PING", event));
        assert(!parse_detect("", event));
    }
    {
        DetectEvent probe;
        probe.source = "src";
        probe.frame_id = 7;
        assert(event_key(probe) == "src:7");
    }

    // Journal: single owner + durability across reopen.
    const std::string dir = temp_dir();
    const std::string path = dir + "/events.journal";
    {
        Journal journal;
        assert(journal.open(path));
        Journal second;
        assert(!second.open(path));  // lock held
        assert(journal.append_if_absent("src:1"));
        assert(!journal.append_if_absent("src:1"));  // atomic duplicate reject
        assert(journal.seen("src:1"));
        assert(!journal.seen("src:2"));
    }
    {
        Journal journal;  // lock released by destructor
        assert(journal.open(path));
        assert(journal.seen("src:1"));
        assert(journal.size() == 1);
    }
    std::filesystem::remove_all(dir);

    // Gateway: accepted, duplicate, invalid, ping, stats.
    {
        const std::string dir2 = temp_dir();
        Journal journal;
        assert(journal.open(dir2 + "/events.journal"));
        Gateway gateway(2, 8);
        const Response accepted = exchange(gateway, journal,
            "DETECT drone-1 42 1000 0.9 10 20 110 120 1920 1080");
        assert(accepted.json.find("\"status\":\"accepted\"") != std::string::npos);

        const Response duplicate = exchange(gateway, journal,
            "DETECT drone-1 42 1000 0.9 10 20 110 120 1920 1080");
        assert(duplicate.json.find("\"status\":\"duplicate\"") != std::string::npos);

        const Response stale = exchange(gateway, journal,
            "DETECT drone-1 43 500 0.9 10 20 110 120 1920 1080");
        assert(stale.json.find("\"status\":\"stale\"") != std::string::npos);

        const Response invalid = exchange(gateway, journal, "UNKNOWN");
        assert(invalid.json == "{\"status\":\"invalid\"}");

        const Response nan = exchange(gateway, journal, "DETECT d 1 1 nan 0 0 1 1 10 10");
        assert(nan.json == "{\"status\":\"invalid\"}");

        const Response ping = exchange(gateway, journal, "PING");
        assert(ping.json == "{\"status\":\"ok\"}");

        const Response stats = exchange(gateway, journal, "STATS");
        assert(stats.json.find("\"accepted\":1") != std::string::npos);
        assert(stats.json.find("\"duplicates\":1") != std::string::npos);
        assert(stats.json.find("\"stale\":1") != std::string::npos);
        assert(stats.json.find("\"invalid\":2") != std::string::npos);
        gateway.stop();
        journal.close();
        std::filesystem::remove_all(dir2);
    }

    // Gateway: concurrent same-key submissions are accepted exactly once.
    {
        const std::string dir3 = temp_dir();
        Journal journal;
        assert(journal.open(dir3 + "/events.journal"));
        Gateway gateway(4, 64);
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::string> statuses;
        gateway.start(&journal, [&](const Response& response) {
            std::lock_guard<std::mutex> lock(mutex);
            statuses.push_back(response.json);
            cv.notify_all();
        });
        const std::string line = "DETECT same-drone 10 1000 0.9 10 20 110 120 1920 1080";
        const int submitters = 12;
        std::vector<std::thread> threads;
        for (int i = 0; i < submitters; ++i)
        {
            threads.emplace_back([&] {
                Request request;
                request.client_id = 1;
                request.line = line;
                gateway.enqueue(std::move(request));
            });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return statuses.size() >= static_cast<std::size_t>(submitters); });
        }
        int accepted = 0;
        int duplicates = 0;
        for (const std::string& status : statuses)
        {
            if (status.find("\"status\":\"accepted\"") != std::string::npos) ++accepted;
            if (status.find("\"status\":\"duplicate\"") != std::string::npos) ++duplicates;
        }
        assert(accepted == 1);
        assert(duplicates == submitters - 1);
        gateway.stop();
        journal.close();
        std::filesystem::remove_all(dir3);
    }

    // Gateway: full queue reports busy (enqueue false).
    {
        Gateway gateway(1, 1);
        // Do not start workers: queue stays full.
        Request first;
        first.line = "PING";
        Request second;
        second.line = "PING";
        assert(gateway.enqueue(std::move(first)));
        assert(!gateway.enqueue(std::move(second)));
    }

    std::puts("core_test passed");
    return 0;
}
