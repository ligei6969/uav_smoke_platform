#include "smoke/evidence_store.hpp"
#include "smoke/stable_event.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {

smoke::FrameStamp Stamp(std::uint64_t frame_id, double time_sec)
{
    smoke::FrameStamp stamp;
    stamp.source = "synthetic-camera";
    stamp.frame_id = frame_id;
    stamp.stream_epoch = 0;
    stamp.time_sec = time_sec;
    stamp.time_base = smoke::TimeBase::Replay;
    stamp.lens = smoke::Lens::Wide;
    return stamp;
}

// C++14 core has no std::filesystem and the existing tests avoid it; these
// file-based cases use std::remove on unique sibling paths instead.
std::string TemporaryPath(const char *suffix)
{
    static int counter = 0;
    return "smoke_events_test_" + std::to_string(++counter) + suffix;
}

void TestTooFewFramesIsNotAnEvent()
{
    smoke::StableEventTracker tracker;
    for (std::uint64_t frame = 1; frame <= 2; ++frame) {
        std::string reason;
        CHECK(tracker.Observe(Stamp(frame, frame * 0.1), true, 0.9, 960, 540, 540, &reason));
    }
    tracker.Flush();
    CHECK(tracker.Events().empty());
}

void TestConsecutiveFramesFormEvent()
{
    smoke::StableEventTracker tracker;
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        std::string reason;
        CHECK(tracker.Observe(Stamp(frame, frame * 0.1), true, frame == 3 ? 0.95 : 0.7,
                              900 + frame, 500, 540, &reason));
    }
    CHECK(tracker.Flush());
    CHECK(tracker.Events().size() == 1);
    const auto &event = tracker.Events().at(0);
    CHECK(event.event_id == 1);
    CHECK(event.first_frame == 1 && event.last_frame == 4);
    CHECK(event.frame_count == 4);
    CHECK(event.max_confidence == 0.95);
    CHECK(std::abs(event.mean_confidence - 0.7625) < 1e-9);
    CHECK(std::abs(event.center_x_px - 902.5) < 1e-9);
    CHECK(event.source == "synthetic-camera");
    CHECK(event.lens == smoke::Lens::Wide);
}

void TestNegativeFrameSplitsRuns()
{
    smoke::StableEventTracker tracker;
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        tracker.Observe(Stamp(frame, frame * 0.1), true, 0.9, 960, 540, 540);
    }
    CHECK(tracker.Observe(Stamp(4, 0.4), false, 0.0, 0, 0, 0));
    for (std::uint64_t frame = 5; frame <= 7; ++frame) {
        tracker.Observe(Stamp(frame, frame * 0.1), true, 0.9, 960, 540, 540);
    }
    tracker.Flush();
    CHECK(tracker.Events().size() == 2);
    CHECK(tracker.Events().at(0).last_frame == 3);
    CHECK(tracker.Events().at(1).first_frame == 5);
    CHECK(tracker.Events().at(1).event_id == 2);
}

void TestGapSplitsRuns()
{
    smoke::StableEventTracker tracker;
    tracker.Observe(Stamp(1, 1.0), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(2, 2.0), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(3, 3.0), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(4, 9.0), true, 0.9, 960, 540, 540); // 6 s gap > 1 s default.
    tracker.Observe(Stamp(5, 9.1), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(6, 9.2), true, 0.9, 960, 540, 540);
    tracker.Flush();
    CHECK(tracker.Events().size() == 2);
}

void TestWeakConfidenceBreaksRun()
{
    smoke::StableEventTracker tracker;
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        tracker.Observe(Stamp(frame, frame * 0.1), true, 0.9, 960, 540, 540);
    }
    tracker.Observe(Stamp(4, 0.4), true, 0.3, 960, 540, 540); // Below the 0.5 gate.
    tracker.Observe(Stamp(5, 0.5), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(6, 0.6), true, 0.9, 960, 540, 540);
    tracker.Observe(Stamp(7, 0.7), true, 0.9, 960, 540, 540);
    tracker.Flush();
    CHECK(tracker.Events().size() == 2);
}

void TestStreamChangeClosesRun()
{
    smoke::StableEventTracker tracker;
    for (std::uint64_t frame = 1; frame <= 3; ++frame) {
        tracker.Observe(Stamp(frame, frame * 0.1), true, 0.9, 960, 540, 540);
    }
    auto changed = Stamp(1, 0.0);
    changed.stream_epoch = 1; // New epoch: a session boundary.
    std::string reason;
    CHECK(tracker.Observe(changed, true, 0.9, 960, 540, 540, &reason));
    for (std::uint64_t frame = 2; frame <= 3; ++frame) {
        auto next = Stamp(frame, frame * 0.1);
        next.stream_epoch = 1;
        tracker.Observe(next, true, 0.9, 960, 540, 540);
    }
    tracker.Flush();
    CHECK(tracker.Events().size() == 2);
}

void TestOrderViolationsRejected()
{
    smoke::StableEventTracker tracker;
    tracker.Observe(Stamp(2, 0.2), true, 0.9, 960, 540, 540);
    std::string reason;
    CHECK(!tracker.Observe(Stamp(2, 0.2), true, 0.9, 960, 540, 540, &reason));
    CHECK(reason == "stale_frame");
    CHECK(!tracker.Observe(Stamp(1, 0.1), true, 0.9, 960, 540, 540, &reason));
    CHECK(!tracker.Observe(Stamp(3, 0.1), true, 0.9, 960, 540, 540, &reason));
    CHECK(reason == "out_of_order_time");
}

void TestEventStoreIsBounded()
{
    smoke::StableEventConfig config;
    config.min_frames = 1;
    config.max_stored_events = 4;
    smoke::StableEventTracker tracker(config);
    for (std::uint64_t frame = 1; frame <= 20; ++frame) {
        tracker.Observe(Stamp(frame, frame * 0.1), frame % 2 == 1, 0.9, 960, 540, 540);
    }
    tracker.Flush();
    CHECK(tracker.Events().size() == 4);
    CHECK(tracker.DroppedEvents() == 6);
    CHECK(tracker.Events().front().event_id == 7);
    CHECK(tracker.Events().back().event_id == 10);
}

void TestEvidenceStoreQuotaAndMarker()
{
    const std::string path = TemporaryPath("-events.jsonl");
    // A previous run may have left artifacts at these unique-per-build paths.
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
    {
        smoke::EvidenceStore store(path, 64);
        CHECK(store.Append("{\"a\":1}"));
        CHECK(store.Append("{\"b\":2}"));
        CHECK(!store.Append("{\"this_record_is_much_too_long_for_the_quota\":1}"));
        CHECK(store.Failed());
        CHECK(!store.Complete());
        CHECK(!store.Append("{}"));
    }
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
}

void TestEvidenceStoreCompleteMarker()
{
    const std::string path = TemporaryPath("-events.jsonl");
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
    {
        smoke::EvidenceStore store(path, 4096);
        CHECK(store.Append("{\"a\":1}"));
        CHECK(store.Complete());
        CHECK(store.Completed());
        CHECK(!store.Append("{}"));
        CHECK(!store.Complete());
    }
    {
        std::ifstream marker(path + ".complete");
        CHECK(marker.good());
    }
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    CHECK(line == "{\"a\":1}");
    std::getline(input, line);
    CHECK(!input.good() || line.empty());
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
}

void TestEvidenceStoreExclusiveCreate()
{
    const std::string path = TemporaryPath("-events.jsonl");
    {
        std::ofstream existing(path);
        existing << "prior content\n";
    }
    {
        smoke::EvidenceStore store(path, 4096);
        CHECK(store.Failed());
        CHECK(!store.Append("{}"));
    }
    std::remove(path.c_str());
}

void TestEvidenceStoreQuotaIsExact()
{
    const std::string path = TemporaryPath("-events.jsonl");
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
    {
        smoke::EvidenceStore store(path, 12);
        CHECK(store.Append("12345678901")); // 11 chars + newline = 12 bytes.
        CHECK(store.Bytes() == 12);
        CHECK(!store.Append("x")); // One more byte would exceed the quota.
        CHECK(store.Failed());     // A failed store never completes: incomplete
        CHECK(!store.Complete());  // evidence must not receive the marker.
        CHECK(store.Bytes() == 12);
    }
    std::ifstream input(path);
    std::string body((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    CHECK(body == "12345678901\n");
    std::remove((path + ".complete").c_str());
    std::remove(path.c_str());
}

} // namespace

int main()
{
    try {
        TestTooFewFramesIsNotAnEvent();
        TestConsecutiveFramesFormEvent();
        TestNegativeFrameSplitsRuns();
        TestGapSplitsRuns();
        TestWeakConfidenceBreaksRun();
        TestStreamChangeClosesRun();
        TestOrderViolationsRejected();
        TestEventStoreIsBounded();
        TestEvidenceStoreQuotaAndMarker();
        TestEvidenceStoreCompleteMarker();
        TestEvidenceStoreExclusiveCreate();
        TestEvidenceStoreQuotaIsExact();
        std::cout << "Events and evidence checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
