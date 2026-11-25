#ifndef SMOKE_REPLAY_IO_HPP
#define SMOKE_REPLAY_IO_HPP

#include "smoke/types.hpp"
#include <istream>
#include <atomic>

namespace smoke {
// Reads a bounded CSV row. No quoting is accepted in this intentionally small format.
bool ReadRow(std::istream &input, std::string &row, const std::atomic<bool> *cancelled = nullptr);
Detection ParseDetectionRow(const std::string &row);
TelemetrySample ParseTelemetryRow(const std::string &row);
std::string JsonString(const std::string &text);
}
#endif
