#include "smoke/h264_stream.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    throw std::runtime_error(std::string("check failed at line ") + \
    std::to_string(__LINE__) + ": " #__VA_ARGS__); } } while (false)

namespace {

int checks = 0;

std::vector<std::uint8_t> Nal(std::uint8_t type, std::size_t payload_size = 8)
{
    // Four-byte start code + NAL header byte + slice-header first bit set
    // (0xC0: ue(v) first_mb_in_slice = 0, a first slice) + filler payload.
    std::vector<std::uint8_t> nal{0, 0, 0, 1, type, 0xC0};
    nal.insert(nal.end(), payload_size - 1, static_cast<std::uint8_t>(type));
    return nal;
}

std::vector<std::uint8_t> NalCont(std::uint8_t type, std::size_t payload_size = 8)
{
    // Continuation slice: first ue(v) bit 0 then 1 => first_mb_in_slice > 0.
    std::vector<std::uint8_t> nal{0, 0, 0, 1, type, 0x40};
    nal.insert(nal.end(), payload_size - 1, static_cast<std::uint8_t>(type));
    return nal;
}

std::vector<std::uint8_t> Join(const std::vector<std::vector<std::uint8_t>> &parts)
{
    std::vector<std::uint8_t> all;
    for (const auto &part : parts) { all.insert(all.end(), part.begin(), part.end()); }
    return all;
}

void TestSpsPpsIdrSliceFormsOneKeyFrame()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(Join({Nal(7), Nal(8), Nal(5)})));
    CHECK(packetizer.Push(NalCont(5))); // Second slice of the same IDR frame.
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 1);
    CHECK(frames[0].sequence == 0);
    CHECK(frames[0].key_frame);
    CHECK(packetizer.FramesEmitted() == 1);
    // Parameter sets are inside the frame payload (decodable as-is).
    CHECK(frames[0].payload.size() ==
          Join({Nal(7), Nal(8), Nal(5), NalCont(5)}).size());
}

void TestInterFramesSplitIntoUnits()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(Join({Nal(7), Nal(8), Nal(5)})));
    CHECK(packetizer.Push(Join({Nal(1), NalCont(1)})));
    CHECK(packetizer.Push(Join({Nal(1)})));
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 3);
    CHECK(frames[0].key_frame && !frames[1].key_frame && !frames[2].key_frame);
    CHECK(frames[0].sequence == 0 && frames[1].sequence == 1 && frames[2].sequence == 2);
}

void TestChunkBoundariesAreTransparent()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    const auto stream = Join({Nal(7), Nal(5), Nal(1), NalCont(1)});
    // One byte at a time through the whole stream.
    for (std::uint8_t byte : stream) {
        CHECK(packetizer.Push(&byte, 1));
    }
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 2);
    CHECK(frames[0].key_frame);
    CHECK(frames[0].payload == Join({Nal(7), Nal(5)}));
    CHECK(frames[1].payload == Join({Nal(1), NalCont(1)}));
}

void TestJunkBeforeFirstStartCodeIsDropped()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    const std::vector<std::uint8_t> junk{0xAB, 0xCD, 0x12, 0x34, 0x00, 0x99};
    CHECK(packetizer.Push(junk));
    CHECK(packetizer.Push(Join({Nal(7), Nal(5), NalCont(5)})));
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 1);
    CHECK(frames[0].payload == Join({Nal(7), Nal(5), NalCont(5)}));
}

void TestFramesBeforeFirstKeyFrameAreDropped()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(Join({Nal(1), Nal(1)}))); // Non-IDR first.
    CHECK(packetizer.FramesEmitted() == 0);
    CHECK(packetizer.Push(Join({Nal(7), Nal(5), NalCont(5)})));
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 1); // Only the key-frame AU survives.
    CHECK(frames[0].key_frame);
    CHECK(packetizer.BytesDropped() > 0);
}

void TestAudDelimiterClosesFrame()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(Join({Nal(7), Nal(5), Nal(9), Nal(1)})));
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 2);
    // AUD closes the IDR frame and opens the next access unit.
    CHECK(frames[0].payload == Join({Nal(7), Nal(5)}));
    CHECK(frames[1].payload == Join({Nal(9), Nal(1)}));
    CHECK(packetizer.FramesEmitted() == 2);
    CHECK(!packetizer.HasPending());
}

void TestOversizedFrameFailsPacketizer()
{
    smoke::H264StreamConfig config;
    config.max_frame_bytes = 32;
    smoke::H264Packetizer packetizer(config);
    std::string reason;
    const auto big = Nal(5, 64); // IDR with a 64-byte payload: over the limit.
    CHECK(packetizer.Push(big)); // Accepted into the reassembly buffer.
    packetizer.Finish();         // End of stream bounds the NAL and sizes it.
    CHECK(packetizer.Failed());  // The oversized frame failed the packetizer.
    CHECK(!packetizer.Push(Nal(1), &reason)); // Failed state is sticky.
    CHECK(packetizer.BytesDropped() > 0);
}

void TestBufferOverflowFailsPacketizer()
{
    smoke::H264StreamConfig config;
    config.max_frame_bytes = 16;
    config.max_buffered_bytes = 32;
    smoke::H264Packetizer packetizer(config);
    std::string reason;
    // Junk without any start code accumulates until the buffer overflows.
    std::vector<std::uint8_t> junk(64, 0xAB);
    CHECK(!packetizer.Push(junk, &reason));
    CHECK(reason == "stream_buffer_overflow");
    CHECK(packetizer.Failed());
}

void TestResetStreamAllowsReconnect()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(Join({Nal(7), Nal(5)})));
    CHECK(packetizer.HasPending());
    packetizer.ResetStream();
    CHECK(!packetizer.HasPending());
    // After a reconnect the first frames again wait for a key frame.
    CHECK(packetizer.Push(Nal(1)));
    CHECK(packetizer.TakeFrames().empty());
    CHECK(packetizer.Push(Join({Nal(5), NalCont(5)})));
    packetizer.Finish();
    auto frames = packetizer.TakeFrames();
    CHECK(frames.size() == 1 && frames[0].key_frame);
    CHECK(frames[0].sequence == 0); // Sequence restarts per stream.
}

void TestEmptyPushAndInvalidConfig()
{
    smoke::H264Packetizer packetizer;
    std::string reason;
    CHECK(packetizer.Push(nullptr, 0));
    bool threw = false;
    try {
        smoke::H264StreamConfig bad;
        bad.max_frame_bytes = 0;
        smoke::H264Packetizer broken(bad);
    } catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
    threw = false;
    try {
        smoke::H264StreamConfig bad;
        bad.max_buffered_bytes = 8; // Below max_frame_bytes.
        smoke::H264Packetizer broken(bad);
    } catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
}

} // namespace

int main()
{
    try {
        TestSpsPpsIdrSliceFormsOneKeyFrame();
        TestInterFramesSplitIntoUnits();
        TestChunkBoundariesAreTransparent();
        TestJunkBeforeFirstStartCodeIsDropped();
        TestFramesBeforeFirstKeyFrameAreDropped();
        TestAudDelimiterClosesFrame();
        TestOversizedFrameFailsPacketizer();
        TestBufferOverflowFailsPacketizer();
        TestResetStreamAllowsReconnect();
        TestEmptyPushAndInvalidConfig();
        std::cout << "H264 stream checks passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
