#ifndef SMOKE_H264_STREAM_HPP
#define SMOKE_H264_STREAM_HPP

#include "smoke/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smoke {

// Recombination of the PSDK live-view H264 callback stream into frames.
// The SDK delivers arbitrary byte chunks (not frame-aligned); this layer
// reassembles NAL units and groups them into access units (frames), with
// explicit frame ownership: each output frame owns its bytes.
//
// Pure logic over bytes — fully testable offline with synthetic NAL data,
// no hardware or SDK activation.

struct H264Frame {
    std::uint64_t sequence = 0;         // 0-based, strictly increasing.
    std::vector<std::uint8_t> payload;  // Complete access unit, own storage.
    bool key_frame = false;             // Contains an IDR NAL.
};

struct H264StreamConfig {
    // Bounded reassembly: largest acceptable frame payload.
    std::size_t max_frame_bytes = 512 * 1024;
    // Bounded stream buffer: chunks are dropped with a reason when exceeded.
    std::size_t max_buffered_bytes = 2 * 1024 * 1024;
    // Expected frame width/height for downstream consumers (metadata only).
    int width = 0;
    int height = 0;
};

// Single-stream, single-owner packetizer. Feed arbitrary chunks; frames come
// out complete. NAL start-code scanning with cross-chunk boundary handling.
class H264Packetizer {
public:
    explicit H264Packetizer(const H264StreamConfig &config = H264StreamConfig());

    // Appends one callback chunk. Returns false with a reason on protocol
    // violations or bound overflow; the packetizer is then failed for good.
    bool Push(const std::uint8_t *data, std::size_t size, std::string *reason = nullptr);
    bool Push(const std::vector<std::uint8_t> &chunk, std::string *reason = nullptr);

    // Drains completed frames in order. Bounded by max_buffered_bytes.
    std::vector<H264Frame> TakeFrames();

    // Closes the trailing access unit (stream end or verification snapshot).
    // A live stream does not call this; the next IDR/AUD closes frames there.
    void Finish();

    // Frames still reassembling (an incomplete trailing access unit).
    bool HasPending() const { return !current_.payload.empty(); }

    std::uint64_t FramesEmitted() const { return frames_emitted_; }
    std::uint64_t BytesDropped() const { return bytes_dropped_; }
    bool Failed() const { return failed_; }

    // Session boundary: drop partial state (a reconnect starts a new stream).
    // Counters survive; sequence numbering restarts at the next key frame.
    void ResetStream();

private:
    // Emits the completed access unit under construction, if any.
    bool StartNewAccessUnit(std::string *reason);

    H264StreamConfig config_;
    std::vector<std::uint8_t> buffer_;   // Unscanned bytes.
    std::vector<H264Frame> ready_;       // Complete frames awaiting TakeFrames.
    H264Frame current_;                  // Access unit under construction.
    std::uint8_t previous_nal_type_ = 0;
    std::size_t scanned_ = 0;           // Bytes of buffer_ already processed.
    bool stream_ended_ = false;         // Set by Finish(): flush the tail NAL.
    std::uint64_t frames_emitted_ = 0;
    std::uint64_t bytes_dropped_ = 0;
    bool failed_ = false;
    bool saw_key_frame_ = false; // Cleared by ResetStream, not by AU close.
};

} // namespace smoke
#endif
