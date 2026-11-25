#include "smoke/h264_stream.hpp"

#include <algorithm>
#include <stdexcept>

namespace smoke {
namespace {

// NAL unit types (H.264). Only what the packetizer needs; the full table
// lives in the spec.
constexpr std::uint8_t kNalIdr = 5;
constexpr std::uint8_t kNalSliceTypeMask = 0x1F;
constexpr std::uint8_t kNalNalTypeMask = 0x1F;
constexpr std::uint8_t kNalSei = 6;
constexpr std::uint8_t kNalSps = 7;
constexpr std::uint8_t kNalPps = 8;
constexpr std::uint8_t kNalAud = 9;

// Finds the next 4-byte or 3-byte start code from offset. Returns the index
// of the first start-code byte and its length, or npos.
constexpr std::size_t kNoStartCode = static_cast<std::size_t>(-1);

std::pair<std::size_t, std::size_t> FindStartCode(const std::vector<std::uint8_t> &data,
                                                  std::size_t offset)
{
    while (offset + 3 <= data.size()) {
        if (data[offset] == 0 && data[offset + 1] == 0) {
            if (data[offset + 2] == 1) { return {offset, 3}; }
            if (offset + 4 <= data.size() && data[offset + 2] == 0 && data[offset + 3] == 1) {
                return {offset, 4};
            }
        }
        ++offset;
    }
    return {kNoStartCode, 0};
}

bool Fail(const char *message, std::string *reason)
{
    if (reason) { *reason = message; }
    return false;
}

// Reads the leading ue(v) coded integer (first_mb_in_slice) from the
// unescaped slice-header bit stream. Returns false when the bits needed are
// not all present yet — the caller then treats the NAL as non-first until
// more bytes arrive (a real first slice never has more than a few leading
// zero bits, so "insufficient data" only happens on tiny mid-NAL chunks).
bool ReadFirstMacroblock(const std::vector<std::uint8_t> &nal, std::size_t header_bytes,
                         bool *first_slice)
{
    // De-emulate in flight, bit-accurate, but only the first 32 bits matter.
    std::uint32_t bits = 0;
    int bit_count = 0;
    int consecutive_zeros = 0;
    for (std::size_t i = header_bytes; i < nal.size() && bit_count < 32; ++i) {
        const std::uint8_t byte = nal[i];
        if (consecutive_zeros == 2 && byte == 3) {
            consecutive_zeros = 0; // Emulation-prevention byte: dropped.
            continue;
        }
        consecutive_zeros = byte == 0 ? consecutive_zeros + 1 : 0;
        for (int bit = 7; bit >= 0 && bit_count < 32; --bit) {
            bits = (bits << 1) | ((byte >> bit) & 1);
            ++bit_count;
        }
    }
    // ue(v): leading zeros, then a 1, then that many more bits. The value is
    // 2^zeros - 1 + suffix; only "is it zero" matters here, which is true
    // exactly when the first bit is 1.
    if (bit_count < 1) { return false; }
    *first_slice = (bits & (1u << 31)) != 0;
    return true;
}

} // namespace

H264Packetizer::H264Packetizer(const H264StreamConfig &config) : config_(config)
{
    if (config.max_frame_bytes == 0 || config.max_frame_bytes > 16 * 1024 * 1024 ||
        config.max_buffered_bytes < config.max_frame_bytes ||
        config.max_buffered_bytes > 64 * 1024 * 1024 ||
        config.width < 0 || config.height < 0 ||
        (config.width == 0) != (config.height == 0)) {
        throw std::invalid_argument("invalid h264 stream configuration");
    }
}

bool H264Packetizer::Push(const std::vector<std::uint8_t> &chunk, std::string *reason)
{
    return Push(chunk.data(), chunk.size(), reason);
}

bool H264Packetizer::Push(const std::uint8_t *data, std::size_t size, std::string *reason)
{
    if (failed_) { return Fail("packetizer_failed", reason); }
    if (data == nullptr && size == 0 && !stream_ended_) {
        // A zero-size push before Finish is a no-op.
        if (reason) { reason->clear(); }
        return true;
    }
    buffer_.insert(buffer_.end(), data, data + size);
    if (buffer_.size() > config_.max_buffered_bytes) {
        bytes_dropped_ += buffer_.size();
        buffer_.clear();
        scanned_ = 0;
        previous_nal_type_ = 0;
        failed_ = true;
        return Fail("stream_buffer_overflow", reason);
    }
    // Incremental scan: scanned_ tracks how much of buffer_ has already been
    // processed, so partial chunks never double-append overlapping bytes.
    while (true) {
        const auto found = FindStartCode(buffer_, scanned_);
        if (found.first == kNoStartCode) {
            // No new NAL: consume junk, but keep the trailing bytes that
            // could form a start code across the next chunk boundary.
            const std::size_t keep = std::min<std::size_t>(buffer_.size(), 3);
            buffer_.erase(buffer_.begin(),
                          buffer_.begin() + static_cast<long>(buffer_.size() - keep));
            scanned_ = 0;
            break;
        }
        const std::size_t nal_start = found.first + found.second;
        const auto next = FindStartCode(buffer_, nal_start);
        // A NAL is only processed once its full payload is present (the next
        // start code bounds it, or the caller signals stream end).
        if (next.first == kNoStartCode && !stream_ended_) {
            // Keep bytes from the NAL start onwards; drop nothing yet.
            buffer_.erase(buffer_.begin(),
                          buffer_.begin() + static_cast<long>(found.first));
            scanned_ = 0;
            break;
        }
        const std::size_t nal_end =
            next.first == kNoStartCode ? buffer_.size() : next.first;
        const std::uint8_t nal_type = buffer_[nal_start] & kNalNalTypeMask;
        const bool nal_is_vcl = nal_type >= 1 && nal_type <= kNalIdr;
        // An IDR begins a new AU only when the current AU already carries
        // VCL data; an SPS/PPS preamble ahead of it belongs to the key frame.
        const bool previous_was_vcl = previous_nal_type_ != 0 &&
            previous_nal_type_ != kNalSps && previous_nal_type_ != kNalPps &&
            previous_nal_type_ != kNalSei && previous_nal_type_ != kNalAud;
        if (nal_type == kNalAud) {
            // Access-unit delimiter closes the previous AU cleanly.
            if (!StartNewAccessUnit(reason)) { return false; }
        } else if (nal_is_vcl && previous_was_vcl) {
            // A VCL NAL starts a new access unit only when it is a first
            // slice (first_mb_in_slice == 0). Continuation slices (value > 0)
            // belong to the access unit under construction. Spec-accurate per
            // ITU-T H.264 7.3.1 without a full slice-header parse.
            bool first_slice = true;
            const std::size_t header_bytes = nal_start + 1 > buffer_.size()
                                                 ? buffer_.size() : nal_start + 1;
            // IDR and non-IDR VCL alike carry a slice header; read the bits.
            const bool have_header_bits =
                ReadFirstMacroblock(buffer_, header_bytes, &first_slice);
            if (have_header_bits && first_slice) {
                if (!StartNewAccessUnit(reason)) { return false; }
            }
        }
        // Append this NAL (start code included) to the current frame.
        current_.payload.insert(current_.payload.end(),
                                buffer_.begin() + static_cast<long>(found.first),
                                buffer_.begin() + static_cast<long>(nal_end));
        current_.key_frame = current_.key_frame || nal_type == kNalIdr;
        previous_nal_type_ = nal_type;
        if (current_.payload.size() > config_.max_frame_bytes) {
            bytes_dropped_ += buffer_.size();
            buffer_.clear();
            scanned_ = 0;
            current_ = H264Frame();
            failed_ = true;
            return Fail("frame_exceeds_size_limit", reason);
        }
        // Consume everything up to this NAL's end from the buffer.
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(nal_end));
        scanned_ = 0;
    }
    if (reason) { reason->clear(); }
    return true;
}

bool H264Packetizer::StartNewAccessUnit(std::string *reason)
{
    if (current_.payload.empty()) {
        if (reason) { reason->clear(); }
        return true;
    }
    // Frames before the stream's first key frame are not decodable: drop
    // them. After that, inter frames decode against the emitted chain.
    if (!saw_key_frame_ && !current_.key_frame) {
        bytes_dropped_ += current_.payload.size();
        current_ = H264Frame();
        if (reason) { reason->clear(); }
        return true;
    }
    saw_key_frame_ = true;
    current_.sequence = frames_emitted_++;
    ready_.push_back(current_);
    current_ = H264Frame();
    if (ready_.size() * config_.max_frame_bytes > config_.max_buffered_bytes &&
        ready_.size() > 64) {
        failed_ = true;
        return Fail("ready_frame_backlog", reason);
    }
    if (reason) { reason->clear(); }
    return true;
}

std::vector<H264Frame> H264Packetizer::TakeFrames()
{
    std::vector<H264Frame> frames;
    frames.swap(ready_);
    return frames;
}

void H264Packetizer::Finish()
{
    stream_ended_ = true;
    // Run the scan once more so the trailing NAL (now bounded by end-of-
    // stream) is appended, then close the access unit under construction.
    Push(nullptr, 0, nullptr);
    StartNewAccessUnit(nullptr);
}

void H264Packetizer::ResetStream()
{
    buffer_.clear();
    current_ = H264Frame();
    ready_.clear();
    previous_nal_type_ = 0;
    scanned_ = 0;
    stream_ended_ = false;
    saw_key_frame_ = false; // A reconnect needs a new key frame anyway.
    // failed_ survives: a protocol failure is a stream defect, not a session
    // boundary. Callers construct a fresh packetizer after a failure.
}

} // namespace smoke
