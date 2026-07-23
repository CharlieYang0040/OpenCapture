#pragma once

#include <string>

namespace opencapture {

struct GifConversionOptions {
    int colors{256};
};

class FFmpegGifConverter final {
public:
    bool Convert(const std::string& inputPath, const std::string& outputPath,
                 GifConversionOptions options = {});
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    void SetError(std::string message);
    std::string lastError_;
};

} // namespace opencapture
