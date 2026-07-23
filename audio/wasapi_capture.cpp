#include "audio/wasapi_capture.h"

#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace opencapture {
namespace {

using Microsoft::WRL::ComPtr;

std::string HResultMessage(HRESULT result) {
    char* message{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, static_cast<DWORD>(result), 0,
                                        reinterpret_cast<char*>(&message), 0, nullptr);
    std::string text = length > 0 && message ? std::string(message, length) : "HRESULT failure";
    if (message) LocalFree(message);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    return text;
}

bool IsFloatFormat(const WAVEFORMATEX* format) {
    if (!format) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

} // namespace

WasapiCapture::~WasapiCapture() { Stop(); }

bool WasapiCapture::Start(AudioEndpointKind endpoint) {
    Stop();
    packets_.Clear();
    packetCount_.store(0, std::memory_order_relaxed);
    droppedPacketCount_.store(0, std::memory_order_relaxed);
    {
        std::scoped_lock lock(stateMutex_);
        format_ = {};
        lastError_.clear();
        setupDone_ = false;
        setupSuccess_ = false;
    }
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        SetError("Could not create the WASAPI stop event.");
        return false;
    }
    thread_ = std::thread([this, endpoint] { CaptureThread(endpoint); });

    std::unique_lock lock(stateMutex_);
    if (!setupCondition_.wait_for(lock, std::chrono::seconds(5), [this] { return setupDone_; })) {
        lastError_ = "WASAPI initialization timed out.";
        lock.unlock();
        Stop();
        return false;
    }
    const bool success = setupSuccess_;
    lock.unlock();
    if (!success) Stop();
    return success;
}

void WasapiCapture::Stop() noexcept {
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    running_.store(false, std::memory_order_release);
}

std::optional<AudioPacket> WasapiCapture::TryPopPacket() { return packets_.TryPop(); }

AudioFormat WasapiCapture::Format() const {
    std::scoped_lock lock(stateMutex_);
    return format_;
}

std::string WasapiCapture::LastError() const {
    std::scoped_lock lock(stateMutex_);
    return lastError_;
}

void WasapiCapture::CaptureThread(AudioEndpointKind endpoint) noexcept {
    const HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(apartmentResult);
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE) {
        FinishSetup(false, "Could not initialize COM for WASAPI: " + HResultMessage(apartmentResult));
        return;
    }

    HANDLE audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> audioClient;
    ComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX* mixFormat{};
    auto fail = [&](std::string operation, HRESULT result) {
        FinishSetup(false, std::move(operation) + ": " + HResultMessage(result));
        if (mixFormat) CoTaskMemFree(mixFormat);
        if (audioEvent) CloseHandle(audioEvent);
        if (uninitialize) CoUninitialize();
    };

    if (!audioEvent) {
        FinishSetup(false, "Could not create the WASAPI sample event.");
        if (uninitialize) CoUninitialize();
        return;
    }
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) return fail("Could not create the audio device enumerator", result);
    const EDataFlow flow = endpoint == AudioEndpointKind::SystemLoopback ? eRender : eCapture;
    const ERole role = endpoint == AudioEndpointKind::SystemLoopback ? eConsole : eCommunications;
    result = enumerator->GetDefaultAudioEndpoint(flow, role, &device);
    if (FAILED(result)) return fail("Could not get the default WASAPI endpoint", result);
    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(result)) return fail("Could not activate the WASAPI audio client", result);
    result = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(result)) return fail("Could not query the WASAPI mix format", result);

    const DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        (endpoint == AudioEndpointKind::SystemLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
    result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, mixFormat, nullptr);
    if (FAILED(result)) return fail("Could not initialize event-driven shared-mode WASAPI", result);
    result = audioClient->SetEventHandle(audioEvent);
    if (FAILED(result)) return fail("Could not set the WASAPI sample event", result);
    result = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(result)) return fail("Could not create the WASAPI capture client", result);

    {
        std::scoped_lock lock(stateMutex_);
        format_ = AudioFormat{mixFormat->nSamplesPerSec, mixFormat->nChannels,
                              mixFormat->wBitsPerSample, mixFormat->nBlockAlign,
                              IsFloatFormat(mixFormat)};
    }
    CoTaskMemFree(mixFormat);
    mixFormat = nullptr;
    result = audioClient->Start();
    if (FAILED(result)) return fail("Could not start WASAPI capture", result);

    running_.store(true, std::memory_order_release);
    FinishSetup(true);
    HANDLE waitHandles[]{stopEvent_, audioEvent};
    bool stopping{};
    while (!stopping) {
        const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
        if (waitResult == WAIT_OBJECT_0) {
            stopping = true;
            continue;
        }
        if (waitResult == WAIT_TIMEOUT) continue;
        if (waitResult != WAIT_OBJECT_0 + 1) {
            SetError("WASAPI event wait failed.");
            break;
        }

        while (!stopping) {
            UINT32 packetFrames{};
            result = captureClient->GetNextPacketSize(&packetFrames);
            if (FAILED(result)) {
                SetError("Could not query the next WASAPI packet: " + HResultMessage(result));
                stopping = true;
                break;
            }
            if (packetFrames == 0) break;
            BYTE* source{};
            DWORD flags{};
            UINT64 devicePosition{};
            UINT64 qpcPosition{};
            result = captureClient->GetBuffer(&source, &packetFrames, &flags, &devicePosition, &qpcPosition);
            if (FAILED(result)) {
                SetError("Could not read a WASAPI packet: " + HResultMessage(result));
                stopping = true;
                break;
            }
            const auto currentFormat = Format();
            AudioPacket packet;
            packet.frameCount = packetFrames;
            packet.qpcPosition100ns = qpcPosition;
            packet.silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            packet.bytes.resize(static_cast<std::size_t>(packetFrames) * currentFormat.blockAlign);
            if (!packet.silent && source) std::memcpy(packet.bytes.data(), source, packet.bytes.size());
            else std::fill(packet.bytes.begin(), packet.bytes.end(), std::uint8_t{});
            result = captureClient->ReleaseBuffer(packetFrames);
            if (FAILED(result)) {
                SetError("Could not release a WASAPI packet: " + HResultMessage(result));
                stopping = true;
                break;
            }
            if (packets_.PushDropOldest(std::move(packet))) {
                droppedPacketCount_.fetch_add(1, std::memory_order_relaxed);
            }
            packetCount_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    audioClient->Stop();
    running_.store(false, std::memory_order_release);
    if (audioEvent) CloseHandle(audioEvent);
    if (uninitialize) CoUninitialize();
}

void WasapiCapture::FinishSetup(bool success, std::string error) {
    {
        std::scoped_lock lock(stateMutex_);
        setupDone_ = true;
        setupSuccess_ = success;
        if (!error.empty()) lastError_ = std::move(error);
    }
    setupCondition_.notify_all();
}

void WasapiCapture::SetError(std::string error) {
    std::scoped_lock lock(stateMutex_);
    lastError_ = std::move(error);
}

} // namespace opencapture
