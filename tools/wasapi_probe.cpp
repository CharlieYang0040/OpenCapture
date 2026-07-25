#include "audio/wasapi_capture.h"

#include <Windows.h>

#include <iostream>

namespace {

bool Probe(const char* name, opencapture::AudioEndpointKind endpoint) {
    opencapture::WasapiCapture capture;
    if (!capture.Start(endpoint)) {
        std::cout << name << "_AVAILABLE=0 ERROR=" << capture.LastError() << '\n';
        return false;
    }
    Sleep(500);
    capture.Stop();
    const auto format = capture.Format();
    std::cout << name << "_AVAILABLE=1"
              << " RATE=" << format.sampleRate
              << " CHANNELS=" << format.channels
              << " BITS=" << format.bitsPerSample
              << " FLOAT=" << format.floatingPoint
              << " PACKETS=" << capture.PacketCount()
              << " DROPPED=" << capture.DroppedPacketCount();
    if (!capture.LastError().empty()) std::cout << " ERROR=" << capture.LastError();
    std::cout << '\n';
    return true;
}

} // namespace

int main() {
    const bool loopback = Probe("SYSTEM_LOOPBACK", opencapture::AudioEndpointKind::SystemLoopback);
    const bool microphone = Probe("MICROPHONE", opencapture::AudioEndpointKind::Microphone);
    return loopback || microphone ? 0 : 2;
}
