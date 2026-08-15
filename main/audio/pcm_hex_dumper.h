#ifndef PCM_HEX_DUMPER_H
#define PCM_HEX_DUMPER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

enum class PcmDumpChannel : uint8_t {
    kMic = 0,
    kReference,
    kAec,
    kUpload,
    kCount,
};

class PcmHexDumper {
public:
    static PcmHexDumper& GetInstance();

    void Arm();
    void Disarm();
    void DumpWhenIdle();
    void Capture(PcmDumpChannel channel, const int16_t* data, size_t sample_count);
    void CaptureInterleaved(PcmDumpChannel channel, const int16_t* data,
                            size_t frame_count, size_t channel_count,
                            size_t channel_index);

private:
    struct ChannelBuffer {
        int16_t* data = nullptr;
        size_t sample_count = 0;
    };

    PcmHexDumper() = default;
    ~PcmHexDumper();
    PcmHexDumper(const PcmHexDumper&) = delete;
    PcmHexDumper& operator=(const PcmHexDumper&) = delete;

    bool InitializeLocked();
    bool HasReferenceSignal(const int16_t* data, size_t sample_count,
                            size_t stride) const;
    void StartDumpTaskLocked();
    void Dump();
    void ReleaseBuffersLocked();
    static void DumpTaskEntry(void* arg);

    std::array<ChannelBuffer, static_cast<size_t>(PcmDumpChannel::kCount)> buffers_{};
    std::mutex mutex_;
    size_t capacity_samples_ = 0;
    size_t reference_signal_samples_ = 0;
    size_t reference_hold_frames_ = 0;
    size_t aec_candidate_frames_ = 0;
    bool initialized_ = false;
    bool armed_ = false;
    bool playback_ready_ = false;
    bool started_ = false;
    bool capture_complete_ = false;
    bool dumping_ = false;
    bool completed_ = false;
};

#endif
