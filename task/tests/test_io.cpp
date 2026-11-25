#include "smoke/replay_io.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    try {
        auto d = smoke::ParseDetectionRow("h30t-1,0,1,1.2,wide,1920,1080,100,200,300,400,0.9");
        CHECK(d.stamp.frame_id == 1 && d.box.confidence == .9);
        CHECK(smoke::LensName(d.stamp.lens) == "wide");
        for (const auto &bad : {
            "h30t-1,0,-1,1.2,wide,1920,1080,100,200,300,400,0.9",
            "h30t-1,0,1,NaN,wide,1920,1080,100,200,300,400,0.9",
            "h30t-1,0,1,1.2,wide,1920,1080,100,200,300,400,1.1",
            "h30t-1,0,1,1.2,wide,1920,1080,300,200,100,400,0.9",
            "h30t-1,0,1,1.2,bad,1920,1080,100,200,300,400,0.9"
        }) {
            bool rejected = false;
            try { smoke::ParseDetectionRow(bad); } catch (const std::exception &) { rejected = true; }
            CHECK(rejected);
        }
        auto s = smoke::ParseTelemetryRow("h30t-1,1,30,120,120,100,-45,0,90,1,0,1");
        CHECK(!s.height_valid && s.gps_valid);
        std::stringstream lines("# example\r\n\nh30t-1\r\n");
        std::string row;
        CHECK(smoke::ReadRow(lines, row) && row == "h30t-1");
        CHECK(!smoke::ReadRow(lines, row));
        std::stringstream long_line(std::string(1025, 'x'));
        bool rejected = false;
        try { smoke::ReadRow(long_line, row); } catch (const std::exception &) { rejected = true; }
        CHECK(rejected);
        CHECK(smoke::JsonString("x\n\"\\") == "\"x\\u000a\\\"\\\\\"");
        std::cout << "IO and validation checks passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
