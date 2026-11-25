#include "smoke/video_pipeline.hpp"
#include "smoke/bounded_queue.hpp"
#include "smoke/target_selector.hpp"
#include "smoke/gimbal.hpp"
#include "smoke/replay_io.hpp"
#include "smoke/video_model.hpp"
#include "smoke/video_report.hpp"

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <openssl/evp.h>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
}

namespace smoke {
namespace video {
namespace {
using Clock = std::chrono::steady_clock;
const char *kModelSha256 = "7bfb2d6745640bd608faae589e5980dbd851b66cacadf356df43fa71053fc077";
volatile std::sig_atomic_t interrupted = 0;
void Interrupt(int) { interrupted = 1; }

struct SignalGuard {
    using Handler = void (*)(int);
    Handler previous_int = std::signal(SIGINT, Interrupt);
    Handler previous_term = std::signal(SIGTERM, Interrupt);
    SignalGuard() { interrupted = 0; }
    ~SignalGuard() { std::signal(SIGINT, previous_int); std::signal(SIGTERM, previous_term); }
};

std::string Path(const std::string &dir, const std::string &file) { return dir + "/" + file; }

std::uint64_t FileSize(const std::string &path)
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
        throw std::runtime_error("expected_regular_file: " + path);
    }
    return static_cast<std::uint64_t>(info.st_size);
}

std::string Sha256(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit);
    if (!input) { throw std::runtime_error("cannot_read_input: " + path); }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("sha256_init_failed");
    }
    char buffer[65536];
    while (input) {
        if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
        input.read(buffer, sizeof(buffer));
        if (input.gcount() && EVP_DigestUpdate(context.get(), buffer, static_cast<std::size_t>(input.gcount())) != 1) {
            throw std::runtime_error("sha256_update_failed");
        }
    }
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest, &size) != 1) { throw std::runtime_error("sha256_final_failed"); }
    std::ostringstream encoded;
    for (unsigned int i = 0; i < size; ++i) { encoded << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]); }
    return encoded.str();
}

void MakeParents(const std::string &dir)
{
    const auto slash = dir.find_last_of('/');
    if (slash == std::string::npos || slash == 0) { return; }
    const auto parent = dir.substr(0, slash);
    struct stat info;
    if (stat(parent.c_str(), &info) == 0) {
        if (!S_ISDIR(info.st_mode)) { throw std::runtime_error("output_parent_is_not_directory"); }
        return;
    }
    if (errno != ENOENT) { throw std::runtime_error("cannot_inspect_output_parent"); }
    MakeParents(parent);
    if (mkdir(parent.c_str(), 0700) != 0 && errno != EEXIST) { throw std::runtime_error("cannot_create_output_parent"); }
}

void Rename(const std::string &from, const std::string &to)
{
    struct stat info;
    if (lstat(to.c_str(), &info) == 0 || errno != ENOENT) { throw std::runtime_error("refusing_to_replace_output"); }
    if (std::rename(from.c_str(), to.c_str()) != 0) { throw std::runtime_error("cannot_publish_output"); }
}

void CheckBudget(const std::string &dir, std::uint64_t limit)
{
    std::uint64_t total = 0;
    for (const char *name : {"annotated_video.part.mp4", "detection_results.part.json", "video_summary.part.json", "target_selection.part.jsonl"}) {
        const auto path = Path(dir, name);
        struct stat info;
        if (stat(path.c_str(), &info) == 0) { total += FileSize(path); }
    }
    for (const char *name : {"first_detection.jpg", "max_confidence.jpg", "last_detection.jpg"}) {
        const auto path = Path(Path(dir, "keyframes.part"), name);
        struct stat info;
        if (stat(path.c_str(), &info) == 0) { total += FileSize(path); }
    }
    if (total > limit) { throw std::runtime_error("output_quota_exceeded"); }
}

struct ProducerGuard {
    std::thread &thread;
    BoundedQueue<VideoFrame> &queue;
    std::atomic<bool> &cancelled;
    void Finish() { cancelled.store(true); queue.Close(); if (thread.joinable()) { thread.join(); } }
    ~ProducerGuard() { Finish(); }
};

std::uint64_t PeakRssBytes()
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) { throw std::runtime_error("cannot_get_peak_rss"); }
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
}

void VerifyCfrPackets(const std::string &path, double fps, std::uint64_t expected)
{
    // OpenCV 4.6 reports POS_MSEC=0 for some valid MP4 codecs. Inspect actual
    // demuxed PTS instead, in bounded memory, including B-frame packet reordering.
    struct Format {
        AVFormatContext *value = nullptr;
        ~Format() { avformat_close_input(&value); }
    } format;
    if (avformat_open_input(&format.value, path.c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(format.value, nullptr) < 0) {
        throw std::runtime_error("cannot_read_video_timestamps");
    }
    int video_stream = -1;
    for (unsigned int i = 0; i < format.value->nb_streams; ++i) {
        if (format.value->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream >= 0) { throw std::runtime_error("requires_one_video_stream"); }
            video_stream = static_cast<int>(i);
        }
    }
    if (video_stream < 0) { throw std::runtime_error("missing_video_stream"); }
    const auto stream = format.value->streams[video_stream];
    const double timebase = av_q2d(stream->time_base);
    if (!std::isfinite(timebase) || timebase <= 0 || stream->start_time == AV_NOPTS_VALUE) {
        throw std::runtime_error("missing_video_timebase_or_start_time");
    }
    struct Packet {
        AVPacket *value = av_packet_alloc();
        ~Packet() { av_packet_free(&value); }
    } packet;
    if (!packet.value) { throw std::bad_alloc(); }
    std::vector<bool> seen(static_cast<std::size_t>(expected), false);
    std::uint64_t count = 0;
    int result = 0;
    while ((result = av_read_frame(format.value, packet.value)) >= 0) {
        if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
        if (packet.value->stream_index == video_stream) {
            if (packet.value->pts == AV_NOPTS_VALUE) { throw std::runtime_error("missing_video_packet_PTS"); }
            if (packet.value->duration <= 0 || std::fabs(packet.value->duration * timebase - 1.0 / fps) > .001) {
                throw std::runtime_error("video_packet_duration_not_CFR");
            }
            const double seconds = static_cast<double>(static_cast<long double>(packet.value->pts) - stream->start_time) * timebase;
            const double index = std::round(seconds * fps);
            if (!std::isfinite(seconds) || index < 0 || index >= static_cast<double>(expected) ||
                std::fabs(seconds - index / fps) > .001 || seen[static_cast<std::size_t>(index)]) {
                throw std::runtime_error("video_PTS_not_CFR_or_duplicate");
            }
            seen[static_cast<std::size_t>(index)] = true;
            ++count;
        }
        av_packet_unref(packet.value);
    }
    if (result != AVERROR_EOF || count != expected) { throw std::runtime_error("video_PTS_count_mismatch"); }
}

std::string BoxJson(const BoundingBox &b)
{
    std::ostringstream json;
    json << std::setprecision(15) << "{\"class_id\":0,\"class_name\":\"smoke\",\"model_class_name\":\"yan\",\"confidence\":"
         << b.confidence << ",\"bbox\":[" << b.x1 << ',' << b.y1 << ',' << b.x2 << ',' << b.y2 << "]}";
    return json.str();
}
}

std::string AnalyzeVideo(const VideoOptions &o)
{
    SignalGuard signal_guard;
    Detection identity;
    identity.stamp.source = o.source; identity.width = 2; identity.height = 2;
    identity.box = {0, 0, 1, 1, 1};
    if (!ValidateDetection(identity) || o.output_dir.empty() || o.output_dir.back() == '/' ||
        !std::isfinite(o.confidence) || !std::isfinite(o.iou) || o.confidence < 0 || o.confidence > 1 ||
        o.iou < 0 || o.iou > 1 || o.max_output_bytes < 1024 || o.max_output_bytes > 1073741824) {
        throw std::invalid_argument("invalid_video_options");
    }
    if (FileSize(o.model) > 64 * 1024 * 1024 || FileSize(o.video) == 0) {
        throw std::invalid_argument("invalid_model_or_video_size");
    }
    const auto model_hash = Sha256(o.model);
    if (model_hash != kModelSha256) { throw std::invalid_argument("model_sha256_does_not_match_confirmed_yan_yun_profile"); }
    const auto video_hash = Sha256(o.video);
    cv::setNumThreads(2);
    cv::ocl::setUseOpenCL(false);
    YoloModel model(o.model);
    cv::VideoCapture capture(o.video, cv::CAP_FFMPEG);
    if (!capture.isOpened()) { throw std::runtime_error("cannot_open_video"); }
    const double fps = capture.get(cv::CAP_PROP_FPS);
    const double width_value = capture.get(cv::CAP_PROP_FRAME_WIDTH);
    const double height_value = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
    const double expected_value = capture.get(cv::CAP_PROP_FRAME_COUNT);
    if (!std::isfinite(fps) || fps <= 0 || fps > 240 || !std::isfinite(width_value) || !std::isfinite(height_value) ||
        width_value < 2 || height_value < 2 || width_value > 1920 || height_value > 1080 ||
        width_value != std::floor(width_value) || height_value != std::floor(height_value) ||
        !std::isfinite(expected_value) || expected_value < 1 || expected_value > 100000000 || expected_value != std::floor(expected_value)) {
        throw std::runtime_error("unsupported_video_metadata_requires_fixed_CFR_up_to_1080p");
    }
    const int width = static_cast<int>(width_value), height = static_cast<int>(height_value);
    const auto expected = static_cast<std::uint64_t>(expected_value);
    VerifyCfrPackets(o.video, fps, expected);
    MakeParents(o.output_dir);
    if (mkdir(o.output_dir.c_str(), 0700) != 0) { throw std::runtime_error("output_directory_must_not_exist"); }
    // Only this invocation owns the newly created directory. Failures below may leave .part files.
    try {
        const auto started = Clock::now();
        const auto video_part = Path(o.output_dir, "annotated_video.part.mp4");
        const auto detection_part = Path(o.output_dir, "detection_results.part.json");
        const auto summary_part = Path(o.output_dir, "video_summary.part.json");
        const auto keyframes_part = Path(o.output_dir, "keyframes.part");
        if (mkdir(keyframes_part.c_str(), 0700) != 0) { throw std::runtime_error("cannot_create_keyframes_directory"); }
        cv::VideoWriter writer(video_part, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('m','p','4','v'), fps, cv::Size(width, height));
        if (!writer.isOpened()) { throw std::runtime_error("cannot_open_video_encoder"); }
        std::ofstream detections(detection_part);
        detections.exceptions(std::ios::badbit | std::ios::failbit);
        detections << std::setprecision(15) << "{\"schema_version\":1,\"video\":" << JsonString(o.video)
            << ",\"model\":" << JsonString(o.model) << ",\"model_sha256\":" << JsonString(model_hash)
            << ",\"model_labels\":{\"0\":\"yan\",\"1\":\"yun\"},\"smoke_class_id\":0"
            << ",\"source\":" << JsonString(o.source) << ",\"lens\":" << JsonString(LensName(o.lens))
            << ",\"stream_epoch\":" << o.stream_epoch
            << ",\"frame_id_base\":0,\"timestamp_basis\":\"cfr_frame_index\",\"fps\":" << fps
            << ",\"frame_count\":" << expected << ",\"width\":" << width << ",\"height\":" << height << ",\"detections\":[";
        const auto target_part = Path(o.output_dir, "target_selection.part.jsonl");
        std::ofstream target_records(target_part);
        target_records.exceptions(std::ios::badbit | std::ios::failbit);
        TargetSelector target_selector;
        GimbalConfig gimbal_config;
        gimbal_config.enabled = true; // Suggestions only; never calls the SDK.
        GimbalController gimbal(gimbal_config, TimeBase::Replay);
        std::uint64_t selected_count = 0;
        std::uint64_t target_changes = 0;
        BoundedQueue<VideoFrame> queue(2);
        std::atomic<bool> cancelled{false}, done{false};
        std::exception_ptr producer_error;
        std::thread producer([&] {
            try {
                std::uint64_t id = 0;
                while (!cancelled.load()) {
                    VideoFrame frame;
                    if (!capture.read(frame.bgr)) { break; }
                    if (frame.bgr.empty() || frame.bgr.type() != CV_8UC3 || frame.bgr.cols != width || frame.bgr.rows != height) {
                        throw std::runtime_error("video_frame_format_or_dimensions_changed");
                    }
                    if (id >= expected) { throw std::runtime_error("video_frame_count_exceeds_metadata"); }
                    frame.stamp.source = o.source; frame.stamp.frame_id = id++;
                    frame.stamp.stream_epoch = o.stream_epoch;
                    frame.stamp.time_sec = static_cast<double>(frame.stamp.frame_id) / fps;
                    frame.stamp.lens = o.lens; frame.stamp.time_base = TimeBase::Replay;
                    while (!cancelled.load() && !queue.PushFor(frame, std::chrono::milliseconds(100))) {}
                }
                if (!cancelled.load() && id != expected) { throw std::runtime_error("video_decoded_frame_count_mismatch"); }
            } catch (...) { producer_error = std::current_exception(); }
            queue.Close(); done.store(true);
        });
        ProducerGuard guard{producer, queue, cancelled};
        SummaryAccumulator stats;
        std::map<std::uint64_t, cv::Mat> images;
        bool first_json = true;
        double inference_seconds = 0;
        std::uint64_t cloud_count = 0;
        for (;;) {
            if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
            VideoFrame frame;
            if (!queue.PopFor(frame, std::chrono::milliseconds(100))) {
                if (done.load() && queue.Size() == 0) { break; }
                continue;
            }
            const auto inference_start = Clock::now();
            const auto all_boxes = model.Infer(frame.bgr, o.confidence, o.iou);
            inference_seconds += std::chrono::duration<double>(Clock::now() - inference_start).count();
            std::vector<BoundingBox> smoke_boxes;
            for (const auto &detection : all_boxes) {
                if (detection.class_id == 0) { smoke_boxes.push_back(detection.box); }
                else { ++cloud_count; }
            }
            stats.Add(frame.stamp.frame_id, frame.stamp.time_sec, smoke_boxes);
            // Process every frame, including no-detection frames. Never reuse an
            // old selected box to keep moving the gimbal after target loss.
            const auto selection = target_selector.Update(frame.stamp, width, height,
                                                           smoke_boxes, frame.stamp.time_sec);
            GimbalDecision steering;
            steering.reason = "no_current_target";
            if (selection.selected_index >= 0) {
                Detection chosen;
                chosen.stamp = frame.stamp; chosen.width = width; chosen.height = height;
                chosen.box = smoke_boxes.at(static_cast<std::size_t>(selection.selected_index));
                steering = gimbal.Update(chosen, frame.stamp.time_sec);
                ++selected_count;
                if (selection.target_changed) { ++target_changes; }
            } else {
                gimbal.Reset();
            }
            target_records << std::setprecision(15) << "{\"frame_id\":" << frame.stamp.frame_id
                << ",\"timestamp_sec\":" << frame.stamp.time_sec << ",\"stream_epoch\":" << frame.stamp.stream_epoch
                << ",\"selected_index\":" << selection.selected_index
                << ",\"reason\":" << JsonString(selection.reason)
                << ",\"target_changed\":" << (selection.target_changed ? "true" : "false")
                << ",\"box\":" << (selection.selected_index < 0 ? "null" :
                    BoxJson(smoke_boxes.at(static_cast<std::size_t>(selection.selected_index))))
                << ",\"gimbal_suggestion_permitted\":" << (steering.permitted ? "true" : "false")
                << ",\"gimbal_reason\":" << JsonString(steering.reason)
                << ",\"yaw_step_deg\":" << steering.yaw_step_deg
                << ",\"pitch_step_deg\":" << steering.pitch_step_deg << ",\"hardware_commands\":0}\n";
            target_records.flush();
            if (!smoke_boxes.empty()) {
                if (!first_json) { detections << ','; }
                first_json = false;
                detections << "{\"frame_id\":" << frame.stamp.frame_id << ",\"timestamp_sec\":" << frame.stamp.time_sec << ",\"boxes\":[";
                for (std::size_t i = 0; i < smoke_boxes.size(); ++i) { if (i) { detections << ','; } detections << BoxJson(smoke_boxes[i]); }
                detections << "]}";
            }
            cv::Mat annotated = frame.bgr; // Exclusive frame ownership in this consumer.
            for (const auto &box : smoke_boxes) {
                cv::rectangle(annotated, cv::Point(cvRound(box.x1), cvRound(box.y1)), cv::Point(cvRound(box.x2), cvRound(box.y2)), cv::Scalar(0,255,0), 2);
                std::ostringstream label; label << "smoke (yan) " << std::fixed << std::setprecision(3) << box.confidence;
                cv::putText(annotated, label.str(), cv::Point(cvRound(box.x1), std::max(15, cvRound(box.y1) - 5)), cv::FONT_HERSHEY_SIMPLEX, .6, cv::Scalar(0,255,0), 2);
            }
            writer.write(annotated);
            const auto selections = stats.KeyframeSelections();
            std::set<std::uint64_t> selected;
            for (const auto &selection : selections) { selected.insert(selection.second); }
            for (auto it = images.begin(); it != images.end();) {
                if (!selected.count(it->first)) { it = images.erase(it); } else { ++it; }
            }
            if (selected.count(frame.stamp.frame_id)) { images[frame.stamp.frame_id] = annotated.clone(); }
            detections.flush();
            CheckBudget(o.output_dir, o.max_output_bytes);
            if (stats.FrameCount() % 300 == 0) {
                std::cerr << "progress frames=" << stats.FrameCount() << '/' << expected << " positive=" << stats.PositiveFrameCount() << '\n';
            }
        }
        guard.Finish(); capture.release();
        if (producer_error) { std::rethrow_exception(producer_error); }
        if (stats.FrameCount() != expected || interrupted) { throw std::runtime_error("analysis_incomplete"); }
        detections << "]}\n"; detections.close(); target_records.close(); writer.release();
        CheckBudget(o.output_dir, o.max_output_bytes);
        // Re-decode every output frame; a successfully opened writer alone is not proof of a valid video.
        cv::VideoCapture verification(video_part, cv::CAP_FFMPEG);
        if (!verification.isOpened() || std::fabs(verification.get(cv::CAP_PROP_FPS) - fps) > .01) {
            throw std::runtime_error("encoded_video_verification_failed");
        }
        std::uint64_t output_count = 0;
        cv::Mat checked;
        while (verification.read(checked)) {
            if (interrupted || checked.cols != width || checked.rows != height || ++output_count > expected) {
                throw std::runtime_error("encoded_video_verification_failed");
            }
        }
        verification.release();
        if (output_count != expected) { throw std::runtime_error("encoded_video_frame_count_mismatch"); }
        if (Sha256(o.model) != model_hash || Sha256(o.video) != video_hash) { throw std::runtime_error("input_changed_during_analysis"); }
        std::ostringstream keyframe_json;
        keyframe_json << '[';
        bool first_keyframe = true;
        for (const auto &selection : stats.KeyframeSelections()) {
            if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
            const auto file = selection.first + ".jpg";
            if (!cv::imwrite(Path(keyframes_part, file), images.at(selection.second))) { throw std::runtime_error("keyframe_write_failed"); }
            if (!first_keyframe) { keyframe_json << ','; } first_keyframe = false;
            keyframe_json << "{\"type\":" << JsonString(selection.first) << ",\"frame_id\":" << selection.second
                << ",\"timestamp_sec\":" << std::setprecision(15) << static_cast<double>(selection.second) / fps
                << ",\"file\":" << JsonString("keyframes/" + file) << '}';
        }
        keyframe_json << ']';
        const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
        std::ostringstream summary;
        summary << std::setprecision(15) << "{\"status\":\"complete\",\"backend\":\"opencv_dnn_cpu\",\"hardware_commands\":0,\"video\":" << JsonString(o.video)
            << ",\"model\":" << JsonString(o.model) << ",\"model_sha256\":" << JsonString(model_hash) << ",\"video_sha256\":" << JsonString(video_hash)
            << ",\"model_labels\":{\"0\":\"yan\",\"1\":\"yun\"},\"smoke_class_id\":0,\"frame_id_base\":0,\"timestamp_basis\":\"cfr_frame_index\""
            << ",\"source\":" << JsonString(o.source) << ",\"lens\":" << JsonString(LensName(o.lens))
            << ",\"stream_epoch\":" << o.stream_epoch
            << ",\"cfr_pts_verified\":true,\"cfr_verification\":\"ffmpeg_packet_pts\",\"cfr_pts_tolerance_sec\":0.001,\"opencv_threads\":2"
            << ",\"max_output_bytes\":" << o.max_output_bytes << ",\"output_budget_mode\":\"post_write_soft_limit\""
            << ",\"preprocessing\":{\"color\":\"RGB\",\"width\":640,\"height\":640,\"padding\":114,\"scale\":0.00392156862745098,\"layout\":\"NCHW\",\"training_contract_verified\":false}"
            << ",\"confidence_threshold\":" << o.confidence << ",\"nms_iou_threshold\":" << o.iou
            << ",\"smoke_detected\":" << (stats.PositiveFrameCount() ? "true" : "false")
            << ",\"fps\":" << fps << ",\"width\":" << width << ",\"height\":" << height
            << ",\"frame_count\":" << stats.FrameCount() << ",\"inferred_frame_count\":" << stats.FrameCount()
            << ",\"output_frame_count\":" << output_count << ",\"duration_sec\":" << static_cast<double>(stats.FrameCount()) / fps
            << ",\"positive_frame_count\":" << stats.PositiveFrameCount() << ",\"positive_frame_ratio\":" << static_cast<double>(stats.PositiveFrameCount()) / stats.FrameCount()
            << ",\"box_count\":" << stats.BoxCount() << ",\"cloud_box_count\":" << cloud_count;
        if (stats.PositiveFrameCount()) {
            summary << ",\"first_detection_sec\":" << stats.FirstTime() << ",\"last_detection_sec\":" << stats.LastTime()
                << ",\"first_detection_frame\":" << stats.FirstFrame() << ",\"last_detection_frame\":" << stats.LastFrame()
                << ",\"max_confidence\":" << stats.MaxConfidence() << ",\"max_confidence_frame\":" << stats.MaxFrame()
                << ",\"mean_confidence\":" << stats.ConfidenceSum() / stats.BoxCount();
        } else {
            summary << ",\"first_detection_sec\":null,\"last_detection_sec\":null,\"first_detection_frame\":null,\"last_detection_frame\":null,\"max_confidence\":null,\"max_confidence_frame\":null,\"mean_confidence\":null";
        }
        summary << ",\"target_selection_file\":\"target_selection.jsonl\",\"selected_frame_count\":" << selected_count
            << ",\"target_changes\":" << target_changes;
        summary << ",\"keyframes\":" << keyframe_json.str() << ",\"queue_capacity\":2,\"queue_high_water\":" << queue.HighWaterMark()
            << ",\"dropped_frames\":" << queue.Dropped() << ",\"elapsed_sec\":" << seconds << ",\"inference_elapsed_sec\":" << inference_seconds
            << ",\"inference_fps\":" << static_cast<double>(stats.FrameCount()) / inference_seconds << ",\"peak_rss_bytes\":" << PeakRssBytes() << '}';
        std::ofstream final_summary(summary_part);
        final_summary.exceptions(std::ios::badbit | std::ios::failbit);
        final_summary << summary.str() << '\n'; final_summary.close();
        CheckBudget(o.output_dir, o.max_output_bytes);
        if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
        Rename(video_part, Path(o.output_dir, "annotated_video.mp4"));
        Rename(detection_part, Path(o.output_dir, "detection_results.json"));
        Rename(target_part, Path(o.output_dir, "target_selection.jsonl"));
        Rename(keyframes_part, Path(o.output_dir, "keyframes"));
        if (interrupted) { throw std::runtime_error("analysis_interrupted"); }
        Rename(summary_part, Path(o.output_dir, "video_summary.json")); // Completion marker is published last.
        return summary.str();
    } catch (const std::exception &error) {
        std::ofstream failure(Path(o.output_dir, "failure.json"));
        failure << "{\"status\":\"failed\",\"hardware_commands\":0,\"error\":" << JsonString(error.what()) << "}\n";
        throw;
    }
}

} // namespace video
} // namespace smoke
