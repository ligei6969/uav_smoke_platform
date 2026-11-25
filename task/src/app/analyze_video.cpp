#include "smoke/video_pipeline.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
void Help()
{
    std::cout << "smoke_analyze_video --video input.mp4 --model best.onnx --output-dir new-directory\n"
                 "  [--source video-11] [--lens unknown|wide|zoom|ir|fpv]\n"
                 "  [--conf 0.5] [--iou 0.45] [--max-output-bytes 1073741824]\n"
                 "Recorded CFR MP4, original yan/yun ONNX, OpenCV CPU, all frames. No aircraft commands.\n"
                 "Output directory must not exist. The final complete summary is the success marker.\n";
}

float Threshold(const std::string &text)
{
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value) || value < 0 || value > 1) {
        throw std::invalid_argument("threshold_must_be_between_zero_and_one");
    }
    return static_cast<float>(value);
}
}

int main(int argc, char **argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { Help(); return 0; }
        const std::map<std::string, bool> allowed = {
            {"--video", true}, {"--model", true}, {"--output-dir", true},
            {"--source", true}, {"--lens", true}, {"--conf", true},
            {"--iou", true}, {"--max-output-bytes", true}};
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; i += 2) {
            const std::string key = argv[i];
            if (!allowed.count(key) || i + 1 >= argc || args.count(key)) {
                throw std::invalid_argument("unknown_missing_or_duplicate_option: " + key);
            }
            args.emplace(key, argv[i + 1]);
        }
        if (!args.count("--video") || !args.count("--model") || !args.count("--output-dir")) {
            throw std::invalid_argument("--video, --model and --output-dir are required");
        }
        smoke::video::VideoOptions options;
        options.video = args.at("--video"); options.model = args.at("--model");
        options.output_dir = args.at("--output-dir");
        if (args.count("--source")) { options.source = args.at("--source"); }
        if (args.count("--lens")) {
            const auto &name = args.at("--lens");
            options.lens = smoke::ParseLens(name);
            if (options.lens == smoke::Lens::Unknown && name != "unknown") {
                throw std::invalid_argument("invalid_lens");
            }
        }
        if (args.count("--conf")) { options.confidence = Threshold(args.at("--conf")); }
        if (args.count("--iou")) { options.iou = Threshold(args.at("--iou")); }
        if (args.count("--max-output-bytes")) {
            const auto &value = args.at("--max-output-bytes");
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("invalid_output_byte_limit");
            }
            options.max_output_bytes = std::stoull(value);
        }
        std::cout << smoke::video::AnalyzeVideo(options) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "smoke_analyze_video: " << error.what() << '\n';
        return 1;
    }
}
