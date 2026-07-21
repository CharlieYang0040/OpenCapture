#pragma once

#include <string_view>

namespace opencapture {

class MainPanel final {
public:
    static void Draw(std::string_view gpuName, std::string_view ffmpegVersion);
};

} // namespace opencapture

