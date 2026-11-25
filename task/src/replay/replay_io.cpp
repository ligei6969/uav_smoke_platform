#include "smoke/replay_io.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace smoke {
namespace {
std::vector<std::string> Fields(const std::string &row, std::size_t expected)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= row.size(); ++i) {
        if (i == row.size() || row[i] == ',') {
            result.push_back(row.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    if (result.size() != expected) { throw std::invalid_argument("wrong_csv_field_count"); }
    for (const auto &field : result) {
        if (field.empty()) { throw std::invalid_argument("empty_csv_field"); }
    }
    return result;
}

double Number(const std::string &field)
{
    std::size_t consumed = 0;
    const double value = std::stod(field, &consumed);
    if (consumed != field.size() || !std::isfinite(value)) {
        throw std::invalid_argument("invalid_numeric_value");
    }
    return value;
}

std::uint64_t Unsigned(const std::string &field)
{
    for (char c : field) {
        if (c < '0' || c > '9') { throw std::invalid_argument("invalid_unsigned_value"); }
    }
    std::size_t consumed = 0;
    const auto value = std::stoull(field, &consumed);
    if (consumed != field.size()) { throw std::invalid_argument("invalid_unsigned_value"); }
    return value;
}

int Dimension(const std::string &field)
{
    auto value = Unsigned(field);
    if (value < 2 || value > 16384) { throw std::invalid_argument("invalid_dimensions"); }
    return static_cast<int>(value);
}

bool Boolean(const std::string &field)
{
    if (field == "1") { return true; }
    if (field == "0") { return false; }
    throw std::invalid_argument("boolean_must_be_zero_or_one");
}
}

bool ReadRow(std::istream &input, std::string &row, const std::atomic<bool> *cancelled)
{
    for (;;) {
        if (cancelled && cancelled->load()) { return false; }
        row.clear();
        char c;
        while (input.get(c)) {
            if (cancelled && cancelled->load()) { return false; }
            if (c == '\n') { break; }
            if (row.size() >= 1024) { throw std::invalid_argument("csv_row_exceeds_1024_bytes"); }
            row.push_back(c);
        }
        if (input.bad()) { throw std::runtime_error("input_read_failed"); }
        if (!row.empty() && row.back() == '\r') { row.pop_back(); }
        if (!row.empty() && row.front() != '#') { return true; }
        if (!input) { return false; }
    }
}

Detection ParseDetectionRow(const std::string &row)
{
    const auto f = Fields(row, 12);
    Detection d;
    d.stamp.source = f[0];
    d.stamp.stream_epoch = Unsigned(f[1]);
    d.stamp.frame_id = Unsigned(f[2]);
    d.stamp.time_sec = Number(f[3]);
    d.stamp.lens = ParseLens(f[4]);
    if (d.stamp.lens == Lens::Unknown && f[4] != "unknown") {
        throw std::invalid_argument("invalid_lens_name");
    }
    d.width = Dimension(f[5]);
    d.height = Dimension(f[6]);
    d.box = {Number(f[7]), Number(f[8]), Number(f[9]), Number(f[10]), Number(f[11])};
    std::string reason;
    if (!ValidateDetection(d, &reason)) { throw std::invalid_argument(reason); }
    return d;
}

TelemetrySample ParseTelemetryRow(const std::string &row)
{
    // 12 columns are the historical contract (epoch 0); 13 columns carry an
    // explicit session epoch. Other widths are rejected: one recording has
    // one contract, and no per-row mixing.
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= row.size(); ++i) {
        if (i == row.size() || row[i] == ',') {
            fields.push_back(row.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    if (fields.size() != 12 && fields.size() != 13) {
        throw std::invalid_argument("wrong_csv_field_count");
    }
    for (const auto &field : fields) {
        if (field.empty()) { throw std::invalid_argument("empty_csv_field"); }
    }
    TelemetrySample s;
    s.source = fields[0];
    s.time_sec = Number(fields[1]);
    if (fields.size() == 13) {
        s.stream_epoch = Unsigned(fields[12]);
    }
    s.latitude_deg = Number(fields[2]); s.longitude_deg = Number(fields[3]);
    s.altitude_m = Number(fields[4]); s.height_agl_m = Number(fields[5]);
    s.pitch_deg = Number(fields[6]); s.roll_deg = Number(fields[7]); s.yaw_deg = Number(fields[8]);
    s.gps_valid = Boolean(fields[9]); s.height_valid = Boolean(fields[10]); s.attitude_valid = Boolean(fields[11]);
    if (s.time_sec < 0.0 || s.source.empty() || s.source.size() > 64) {
        throw std::invalid_argument("invalid_telemetry_identity");
    }
    return s;
}

std::string JsonString(const std::string &text)
{
    std::ostringstream stream;
    stream << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { stream << '\\' << c; }
        else if (c < 0x20) { stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec; }
        else { stream << c; }
    }
    stream << '"';
    return stream.str();
}
} // namespace smoke
