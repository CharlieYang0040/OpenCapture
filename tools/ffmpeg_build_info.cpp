extern "C" {
#include <libavutil/avutil.h>
}

#include <iostream>

int main() {
    std::cout << "FFmpeg version: " << av_version_info() << '\n'
              << "configuration:\n"
              << avutil_configuration() << '\n';
    return 0;
}
