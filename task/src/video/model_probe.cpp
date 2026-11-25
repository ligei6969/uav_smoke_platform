#include "smoke/video_model.hpp"

#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

class ExclusiveFile {
public:
    explicit ExclusiveFile(const std::string &path) : path_(path)
    {
#ifdef _WIN32
        descriptor_ = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                            _S_IREAD | _S_IWRITE);
#else
        descriptor_ = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
        if (descriptor_ < 0) { throw std::runtime_error("cannot_create_output_exclusively: " + path); }
    }

    ~ExclusiveFile()
    {
        if (descriptor_ >= 0) { CloseDescriptor(); }
        if (!keep_) { std::remove(path_.c_str()); }
    }

    ExclusiveFile(const ExclusiveFile &) = delete;
    ExclusiveFile &operator=(const ExclusiveFile &) = delete;

    void Write(const cv::Mat &values)
    {
        if (!values.isContinuous() || values.type() != CV_32FC1) {
            throw std::runtime_error("expected_contiguous_float32_tensor");
        }
        const unsigned char *data = values.ptr<unsigned char>();
        std::size_t remaining = values.total() * sizeof(float);
        while (remaining != 0) {
            const std::size_t chunk = std::min<std::size_t>(remaining, 1048576);
#ifdef _WIN32
            const int written = _write(descriptor_, data, static_cast<unsigned int>(chunk));
#else
            const ssize_t written = write(descriptor_, data, chunk);
#endif
            if (written < 0 && errno == EINTR) { continue; }
            if (written <= 0) { throw std::runtime_error("cannot_write_probe_tensor"); }
            data += written;
            remaining -= static_cast<std::size_t>(written);
        }
    }

    void Finish()
    {
        const int result = CloseDescriptor();
        descriptor_ = -1;
        if (result != 0) { throw std::runtime_error("cannot_close_probe_tensor"); }
    }

    void Keep() { keep_ = true; }

private:
    int CloseDescriptor()
    {
#ifdef _WIN32
        return _close(descriptor_);
#else
        return close(descriptor_);
#endif
    }

    std::string path_;
    int descriptor_ = -1;
    bool keep_ = false;
};

} // namespace

int main(int argc, char **argv)
{
    try {
        std::string model_path;
        std::string image_path;
        std::string prefix;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (index + 1 >= argc) { throw std::invalid_argument("missing_probe_argument_value"); }
            const std::string value = argv[++index];
            std::string *destination = nullptr;
            if (option == "--model") { destination = &model_path; }
            else if (option == "--image") { destination = &image_path; }
            else if (option == "--output-prefix") { destination = &prefix; }
            else { throw std::invalid_argument("unknown_probe_option: " + option); }
            if (!destination->empty()) { throw std::invalid_argument("duplicate_probe_option: " + option); }
            *destination = value;
        }
        if (model_path.empty() || image_path.empty() || prefix.empty()) {
            throw std::invalid_argument("usage: model_probe --model MODEL --image PNG --output-prefix PREFIX");
        }
        static_assert(sizeof(float) == 4, "Probe format requires 32-bit float");
        const std::uint32_t marker = 1;
        if (*reinterpret_cast<const unsigned char *>(&marker) != 1) {
            throw std::runtime_error("probe_requires_little_endian_float32");
        }
        ExclusiveFile input_file(prefix + ".input.bin");
        ExclusiveFile output_file(prefix + ".output.bin");
        const cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
        const auto prepared = smoke::video::PrepareInput(image);
        smoke::video::YoloModel model(model_path);
        const cv::Mat output = model.ForwardRaw(image);
        input_file.Write(prepared.blob);
        output_file.Write(output);
        input_file.Finish();
        output_file.Finish();
        input_file.Keep();
        output_file.Keep();
        std::cout << "{\"input_shape\":[1,3,640,640],\"output_shape\":[1,25200,7],"
                     "\"input_floats\":" << prepared.blob.total()
                  << ",\"output_floats\":" << output.total()
                  << ",\"dtype\":\"float32_le\",\"backend\":\"opencv_cpu\"}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "model_probe: " << error.what() << '\n';
        return 1;
    }
}
