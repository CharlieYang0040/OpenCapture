#pragma once

#include <functional>
#include <string>
#include <stop_token>

namespace opencapture {

struct GifConversionOptions {
    int colors{256};
};

using GifProgressCallback = std::function<void(double)>;

class FFmpegGifConverter final {
public:
    bool Convert(const std::string& inputPath, const std::string& outputPath,
                 GifConversionOptions options = {}, std::stop_token stopToken = {},
                 GifProgressCallback progress = {});
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    void SetError(std::string message);
    std::string lastError_;
};

} // namespace opencapture
