#include "smoke/bounded_queue.hpp"
#include "smoke/evidence_store.hpp"
#include "smoke/gimbal.hpp"
#include "smoke/localization.hpp"
#include "smoke/replay_io.hpp"
#include "smoke/stable_event.hpp"
#include "smoke/telemetry.hpp"

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

void CheckInputFile(const std::string &path)
{
#ifdef _WIN32
    struct _stat64 info;
    if (_stat64(path.c_str(), &info) != 0 || (info.st_mode & _S_IFMT) != _S_IFREG) {
#else
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
#endif
        throw std::invalid_argument("input_must_be_a_regular_file");
    }
    if (info.st_size < 0 || info.st_size > 64 * 1024 * 1024) {
        throw std::invalid_argument("metadata_input_exceeds_64MiB");
    }
}

class ExclusiveOutput {
public:
    explicit ExclusiveOutput(const std::string &path)
    {
#ifdef _WIN32
        const int fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd < 0) { throw std::runtime_error("cannot_create_output_exclusively"); }
        file_ = _fdopen(fd, "wb");
        if (!file_) { _close(fd); throw std::runtime_error("cannot_open_output_stream"); }
#else
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) { throw std::runtime_error("cannot_create_output_exclusively"); }
        file_ = fdopen(fd, "wb");
        if (!file_) { close(fd); throw std::runtime_error("cannot_open_output_stream"); }
#endif
    }
    ~ExclusiveOutput() { if (file_) { std::fclose(file_); } }
    ExclusiveOutput(const ExclusiveOutput &) = delete;
    ExclusiveOutput &operator=(const ExclusiveOutput &) = delete;
    void Write(const std::string &line)
    {
        if (std::fwrite(line.data(), 1, line.size(), file_) != line.size()) {
            throw std::runtime_error("output_write_failed");
        }
    }
    void Flush()
    {
        if (std::fflush(file_) != 0) { throw std::runtime_error("output_flush_failed"); }
    }
private:
    std::FILE *file_ = nullptr;
};

struct ProducerGuard {
    std::thread &producer;
    smoke::BoundedQueue<smoke::Detection> &queue;
    std::atomic<bool> &cancelled;
    ~ProducerGuard()
    {
        cancelled.store(true);
        queue.Close();
        if (producer.joinable()) { producer.join(); }
    }
};

void Help()
{
    std::cout << "smoke_replay --detections detections.csv --output evaluation.jsonl\n"
                 "  [--telemetry telemetry.csv] [--lens wide|zoom|ir]\n"
                 "  [--calibrated-hfov degrees] [--max-time-delta seconds]\n"
                 "  [--queue-capacity 8] [--max-output-bytes 16777216]\n"
                 "  [--events-output events.jsonl] [--min-event-frames 3]\n"
                 "                 [--max-event-bytes 1048576]\n"
                 "Consumes recorded detection METADATA; does not run inference or actuate hardware.\n"
                 "No calibration or telemetry => localization explicitly invalid. All control is dry-run.\n"
                 "Events are stability decisions over metadata; evidence is not a fire report.\n";
}

std::map<std::string, std::string> Options(int argc, char **argv)
{
    const std::map<std::string, std::string> allowed = {
        {"--detections", ""}, {"--output", ""}, {"--telemetry", ""},
        {"--lens", ""}, {"--calibrated-hfov", ""}, {"--max-time-delta", ""},
        {"--queue-capacity", ""}, {"--max-output-bytes", ""},
        {"--events-output", ""}, {"--min-event-frames", ""},
        {"--max-event-bytes", ""}
    };
    std::map<std::string, std::string> options;
    for (int i = 1; i < argc; i += 2) {
        const std::string key = argv[i];
        if (!allowed.count(key) || i + 1 >= argc || options.count(key)) {
            throw std::invalid_argument("unknown, missing or duplicate option: " + key);
        }
        options.emplace(key, argv[i + 1]);
    }
    if (!options.count("--detections") || !options.count("--output")) {
        throw std::invalid_argument("--detections and --output are required");
    }
    return options;
}

double NumericOption(const std::map<std::string, std::string> &options, const char *name, double fallback)
{
    const auto found = options.find(name);
    if (found == options.end()) { return fallback; }
    std::size_t length = 0;
    const double value = std::stod(found->second, &length);
    if (length != found->second.size() || !std::isfinite(value)) {
        throw std::invalid_argument(std::string("invalid option: ") + name);
    }
    return value;
}

// One evidence record: the stability decision plus audit fields. Hardware
// commands remain zero; models are named so consumers cannot mistake this
// for a fire report or a tracked real-world object.
std::string EvidenceLine(const smoke::StableEvent &event, std::uint64_t min_frames)
{
    std::ostringstream row;
    row << std::setprecision(15)
        << "{\"event_id\":" << event.event_id
        << ",\"source\":" << smoke::JsonString(event.source)
        << ",\"stream_epoch\":" << event.stream_epoch
        << ",\"lens\":" << smoke::JsonString(smoke::LensName(event.lens))
        << ",\"first_frame\":" << event.first_frame
        << ",\"last_frame\":" << event.last_frame
        << ",\"first_time_sec\":" << event.first_time_sec
        << ",\"last_time_sec\":" << event.last_time_sec
        << ",\"frame_count\":" << event.frame_count
        << ",\"max_confidence\":" << event.max_confidence
        << ",\"mean_confidence\":" << event.mean_confidence
        << ",\"center_x_px\":" << event.center_x_px
        << ",\"center_y_px\":" << event.center_y_px
        << ",\"bottom_center_y_px\":" << event.bottom_center_y_px
        << ",\"min_frames\":" << min_frames
        << ",\"event_model\":\"metadata_stability_decision\""
        << ",\"ground_projection\":\"flat_ground_hypothesis\""
        << ",\"hardware_commands\":0}";
    return row.str();
}
}

int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { Help(); return 0; }
        const auto options = Options(argc, argv);
        const double capacity = NumericOption(options, "--queue-capacity", 8);
        const double max_delta = NumericOption(options, "--max-time-delta", 0.2);
        const double byte_limit = NumericOption(options, "--max-output-bytes", 16 * 1024 * 1024);
        const double min_event_frames = NumericOption(options, "--min-event-frames", 3);
        const double max_event_bytes = NumericOption(options, "--max-event-bytes", 1024 * 1024);
        if (capacity < 1 || capacity > 4096 || capacity != std::floor(capacity) ||
            max_delta < 0 || max_delta > 10 || byte_limit < 1024 || byte_limit > 1073741824 ||
            byte_limit != std::floor(byte_limit) ||
            min_event_frames < 1 || min_event_frames > 4096 ||
            min_event_frames != std::floor(min_event_frames) ||
            max_event_bytes < 1024 || max_event_bytes > 1073741824 ||
            max_event_bytes != std::floor(max_event_bytes)) {
            throw std::invalid_argument("option outside resource limits");
        }
        smoke::CameraCalibration calibration;
        if (options.count("--calibrated-hfov") || options.count("--lens")) {
            if (!options.count("--calibrated-hfov") || !options.count("--lens")) {
                throw std::invalid_argument("--lens and --calibrated-hfov must be supplied together");
            }
            calibration.lens = smoke::ParseLens(options.at("--lens"));
            calibration.horizontal_fov_deg = NumericOption(options, "--calibrated-hfov", 0);
            calibration.calibrated = true;
            if (calibration.lens == smoke::Lens::Unknown || calibration.lens == smoke::Lens::Fpv ||
                calibration.horizontal_fov_deg <= 1 || calibration.horizontal_fov_deg >= 179) {
                throw std::invalid_argument("invalid calibrated lens or horizontal FOV");
            }
        }
        const auto input_name = options.at("--detections");
        const auto output_name = options.at("--output");
        if (input_name == output_name || (options.count("--telemetry") && options.at("--telemetry") == output_name)) {
            throw std::invalid_argument("output_must_not_replace_input");
        }
        const bool emit_events = options.count("--events-output") > 0;
        const std::string events_name = emit_events ? options.at("--events-output") : std::string();
        if (emit_events && (events_name == output_name || events_name == input_name ||
                            (options.count("--telemetry") && events_name == options.at("--telemetry")))) {
            throw std::invalid_argument("events_output_must_differ_from_other_files");
        }
        CheckInputFile(input_name);
        std::ifstream input(input_name);
        if (!input) { throw std::runtime_error("cannot_open_detections"); }
        std::ifstream telemetry_input;
        if (options.count("--telemetry")) {
            CheckInputFile(options.at("--telemetry"));
            telemetry_input.open(options.at("--telemetry"));
            if (!telemetry_input) { throw std::runtime_error("cannot_open_telemetry"); }
        }
        ExclusiveOutput output(output_name);
        // The evidence store is created lazily at the first event record so a
        // run with zero events leaves no incomplete artifact on disk.
        std::unique_ptr<smoke::EvidenceStore> evidence;
        auto ensure_evidence = [&]() {
            if (!evidence) {
                evidence.reset(new smoke::EvidenceStore(
                    events_name, static_cast<std::uint64_t>(max_event_bytes)));
                if (evidence->Failed()) {
                    throw std::runtime_error("cannot_create_events_output_exclusively");
                }
            }
        };
        smoke::StableEventConfig event_config;
        event_config.min_frames = static_cast<std::uint64_t>(min_event_frames);
        smoke::StableEventTracker tracker(event_config);

        smoke::BoundedQueue<smoke::Detection> queue(static_cast<std::size_t>(capacity));
        std::exception_ptr producer_error;
        std::atomic<bool> cancelled{false};
        std::atomic<bool> producer_done{false};
        std::thread producer([&] {
            try {
                std::string row;
                double last_time = -1.0;
                std::string source;
                std::uint64_t last_id = 0;
                smoke::FrameStamp session;
                bool first = true;
                while (smoke::ReadRow(input, row, &cancelled)) {
                    auto detection = smoke::ParseDetectionRow(row);
                    if ((!first && (detection.stamp.source != source || detection.stamp.time_sec < last_time ||
                                    detection.stamp.frame_id <= last_id ||
                                    detection.stamp.stream_epoch != session.stream_epoch ||
                                    detection.stamp.lens != session.lens))) {
                        throw std::invalid_argument("replay_requires_one_source_epoch_lens_and_increasing_frames");
                    }
                    session = detection.stamp;
                    first = false; source = detection.stamp.source;
                    last_time = detection.stamp.time_sec; last_id = detection.stamp.frame_id;
                    while (!queue.PushFor(detection, std::chrono::milliseconds(100))) {
                        // Close is observed via the consumer's exception path. A separate flag is below.
                        if (cancelled.load()) { break; }
                    }
                    if (cancelled.load()) { break; }
                }
            } catch (...) { producer_error = std::current_exception(); }
            queue.Close();
            producer_done.store(true);
        });
        ProducerGuard producer_guard{producer, queue, cancelled};
        // Processing errors must cancel the producer before its join.
        auto finish = [&] { cancelled.store(true); queue.Close(); if (producer.joinable()) { producer.join(); } };
        std::size_t processed = 0, located = 0, proposed = 0, output_bytes = 0;
        std::size_t events_emitted = 0;
        std::size_t events_before = static_cast<std::size_t>(tracker.Events().size());
        // A sorted single-session replay only needs the nearest before/after candidates.
        smoke::TelemetryBuffer telemetry(2);
        smoke::GimbalConfig gimbal_config;
        gimbal_config.enabled = true; // Suggestions only: there is no hardware executor in this binary.
        smoke::GimbalController gimbal(gimbal_config, smoke::TimeBase::Replay);
        smoke::TelemetrySample next_telemetry;
        bool has_next = false;
        bool telemetry_started = false;
        std::string telemetry_source;
        double previous_telemetry_time = -1.0;
        std::string telemetry_row;
        auto read_next = [&] {
            has_next = false;
            while (telemetry_input.is_open() && smoke::ReadRow(telemetry_input, telemetry_row)) {
                next_telemetry = smoke::ParseTelemetryRow(telemetry_row);
                if (next_telemetry.time_sec < previous_telemetry_time) { throw std::invalid_argument("telemetry_csv_must_be_time_sorted"); }
                previous_telemetry_time = next_telemetry.time_sec;
                if (next_telemetry.source == telemetry_source) { has_next = true; break; }
            }
        };
        const auto started = Clock::now();
        try {
            smoke::Detection detection;
            for (;;) {
                if (!queue.PopFor(detection, std::chrono::milliseconds(100))) {
                    if (producer_done.load() && queue.Size() == 0) { break; }
                    continue;
                }
                if (!telemetry_started) {
                    telemetry_source = detection.stamp.source;
                    telemetry_started = true;
                    read_next();
                }
                while (has_next && next_telemetry.time_sec <= detection.stamp.time_sec) {
                    std::string reason;
                    if (!telemetry.Push(next_telemetry, &reason)) { throw std::invalid_argument("telemetry: " + reason); }
                    read_next();
                }
                if (has_next) {
                    std::string reason;
                    if (!telemetry.Push(next_telemetry, &reason)) { throw std::invalid_argument("telemetry: " + reason); }
                }
                const auto match = telemetry.Match(detection.stamp, max_delta);
                const auto location = smoke::Locate(detection, match, calibration);
                const auto control = gimbal.Update(detection, detection.stamp.time_sec);
                {
                    std::string event_reason;
                    // The tracker applies its own confidence gate; positive=true means
                    // "this frame contains a smoke box", not "it is strong".
                    // Box bottom-center is the point ground projection consumes.
                    if (!tracker.Observe(detection.stamp, true, detection.box.confidence,
                                         (detection.box.x1 + detection.box.x2) / 2.0,
                                         (detection.box.y1 + detection.box.y2) / 2.0,
                                         detection.box.y2, &event_reason)) {
                        throw std::invalid_argument("events: " + event_reason);
                    }
                    const std::size_t events_now = tracker.Events().size();
                    for (std::size_t index = events_before; index < events_now; ++index) {
                        if (emit_events) {
                            ensure_evidence();
                            if (!evidence->Append(EvidenceLine(tracker.Events().at(index),
                                                               static_cast<std::uint64_t>(min_event_frames)))) {
                                throw std::runtime_error("evidence_store_quota_or_write_failed");
                            }
                        }
                        ++events_emitted;
                    }
                    events_before = events_now;
                }
                std::ostringstream row;
                row << std::setprecision(15)
                    << "{\"source\":" << smoke::JsonString(detection.stamp.source)
                    << ",\"frame_id\":" << detection.stamp.frame_id
                    << ",\"stream_epoch\":" << detection.stamp.stream_epoch
                    << ",\"lens\":" << smoke::JsonString(smoke::LensName(detection.stamp.lens))
                    << ",\"timestamp_sec\":" << detection.stamp.time_sec
                    << ",\"location_valid\":" << (location.valid ? "true" : "false")
                    << ",\"location_reason\":" << smoke::JsonString(location.reason);
                if (location.valid) {
                    row << ",\"latitude_deg\":" << location.latitude_deg << ",\"longitude_deg\":" << location.longitude_deg;
                }
                row << ",\"location_model\":\"flat_ground_hypothesis\",\"control_mode\":\"dry_run\""
                    << ",\"control_proposed\":" << (control.permitted ? "true" : "false")
                    << ",\"control_reason\":" << smoke::JsonString(control.reason)
                    << ",\"yaw_step_deg\":" << control.yaw_step_deg
                    << ",\"pitch_step_deg\":" << control.pitch_step_deg << "}\n";
                const auto encoded = row.str();
                if (encoded.size() > static_cast<std::size_t>(byte_limit) - output_bytes) { throw std::runtime_error("output_quota_exceeded"); }
                output.Write(encoded);
                output_bytes += encoded.size(); ++processed;
                if (location.valid) { ++located; }
                if (control.permitted) { ++proposed; }
            }
            finish();
            if (producer_error) { std::rethrow_exception(producer_error); }
            // Session end closes any open run; a run with enough frames emits one event.
            tracker.Flush();
            if (emit_events && tracker.Events().size() > events_before) {
                for (std::size_t index = events_before; index < tracker.Events().size(); ++index) {
                    ensure_evidence();
                    if (!evidence->Append(EvidenceLine(tracker.Events().at(index),
                                                       static_cast<std::uint64_t>(min_event_frames)))) {
                        throw std::runtime_error("evidence_store_quota_or_write_failed");
                    }
                    ++events_emitted;
                }
                events_before = tracker.Events().size();
                if (!evidence->Complete()) {
                    throw std::runtime_error("evidence_store_completion_failed");
                }
            }
            output.Flush();
        } catch (...) { finish(); throw; }
        const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
        std::cout << "{\"mode\":\"metadata_replay\",\"hardware_commands\":0,\"processed\":" << processed
                  << ",\"located\":" << located << ",\"control_proposals\":" << proposed
                  << ",\"events_emitted\":" << events_emitted
                  << ",\"queue_high_water\":" << queue.HighWaterMark()
                  << ",\"elapsed_sec\":" << seconds << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "smoke_replay: " << error.what() << '\n';
        return 1;
    }
}
