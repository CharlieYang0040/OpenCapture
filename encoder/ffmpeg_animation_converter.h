#pragma once

#include "core/recording_options.h"

#include <functional>
#include <stop_token>
#include <string>

namespace opencapture {

struct AnimationConversionOptions {
    AnimationFormat format{AnimationFormat::WebP};
    int quality{82};
    int avifCrf{34};
};

class FFmpegAnimationConverter final {
public:
    bool Convert(const std::string& inputPath, const std::string& outputPath,
                 AnimationConversionOptions options, std::stop_token stopToken = {},
                 std::function<void(double)> progress = {});
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

private:
    std::string lastError_;
};

} // namespace opencapture
