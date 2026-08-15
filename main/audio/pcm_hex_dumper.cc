#include "pcm_hex_dumper.h"

#include "sdkconfig.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "PcmHexDumper"

namespace {

constexpr size_t kSampleRate = 16000;
constexpr size_t kBytesPerLine = 128;
constexpr size_t kReferenceWarmupSamples = kSampleRate * 3 / 2;
constexpr size_t kReferenceHoldFrames = 3;
constexpr size_t kAecTriggerFrames = 2;
constexpr int32_t kReferenceStartThreshold = 64;
// Pure far-end residuals in the latest aligned capture reached about 120.
// Keep the trigger above that floor so the dump starts on real near-end speech.
constexpr int32_t kAecTriggerAverage = 250;

constexpr const char* kChannelNames[] = {
    "MIC",
    "REF",
    "AEC",
    "UPLOAD",
};

}  // namespace

PcmHexDumper& PcmHexDumper::GetInstance() {
    static PcmHexDumper instance;
    return instance;
}

PcmHexDumper::~PcmHexDumper() {
#if CONFIG_AEC_PCM_HEX_DUMP
    std::lock_guard<std::mutex> lock(mutex_);
    ReleaseBuffersLocked();
#endif
}

bool PcmHexDumper::InitializeLocked() {
#if CONFIG_AEC_PCM_HEX_DUMP
    if (initialized_) {
        return capacity_samples_ > 0;
    }

    initialized_ = true;
    capacity_samples_ = kSampleRate * CONFIG_AEC_PCM_HEX_DUMP_SECONDS;
    const size_t bytes = capacity_samples_ * sizeof(int16_t);

    for (auto& buffer : buffers_) {
        buffer.data = static_cast<int16_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer.data == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate %u bytes per PCM channel",
                     static_cast<unsigned>(bytes));
            ReleaseBuffersLocked();
            capacity_samples_ = 0;
            return false;
        }
    }

    ESP_LOGI(TAG, "Buffers ready, capture=%d seconds",
             CONFIG_AEC_PCM_HEX_DUMP_SECONDS);
    return true;
#else
    return false;
#endif
}

bool PcmHexDumper::HasReferenceSignal(const int16_t* data, size_t sample_count,
                                      size_t stride) const {
    for (size_t i = 0; i < sample_count; ++i) {
        const int32_t sample = data[i * stride];
        const int32_t level = sample < 0 ? -sample : sample;
        if (level > kReferenceStartThreshold) {
            return true;
        }
    }
    return false;
}

void PcmHexDumper::Arm() {
#if CONFIG_AEC_PCM_HEX_DUMP
    std::lock_guard<std::mutex> lock(mutex_);
    if (armed_ || started_ || capture_complete_ || dumping_ || completed_) {
        return;
    }
    if (!InitializeLocked()) {
        return;
    }

    reference_signal_samples_ = 0;
    reference_hold_frames_ = 0;
    aec_candidate_frames_ = 0;
    playback_ready_ = false;
    for (auto& buffer : buffers_) {
        buffer.sample_count = 0;
    }
    armed_ = true;
    ESP_LOGI(TAG, "Armed on speaking state; waiting for playback and double-talk");
#endif
}

void PcmHexDumper::Disarm() {
#if CONFIG_AEC_PCM_HEX_DUMP
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed_ || started_) {
        return;
    }

    armed_ = false;
    reference_signal_samples_ = 0;
    reference_hold_frames_ = 0;
    aec_candidate_frames_ = 0;
    playback_ready_ = false;
    for (auto& buffer : buffers_) {
        buffer.sample_count = 0;
    }
    ESP_LOGI(TAG, "Short speaking segment ignored; PCM capture disarmed");
#endif
}

void PcmHexDumper::DumpWhenIdle() {
#if CONFIG_AEC_PCM_HEX_DUMP
    std::lock_guard<std::mutex> lock(mutex_);
    if (!capture_complete_ || dumping_ || completed_) {
        return;
    }

    dumping_ = true;
    if (xTaskCreate(DumpTaskEntry, "pcm_hex_dump", 4096, this, 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PCM dump task");
        dumping_ = false;
        completed_ = true;
        ReleaseBuffersLocked();
    }
#endif
}

void PcmHexDumper::Capture(PcmDumpChannel channel, const int16_t* data,
                           size_t sample_count) {
#if CONFIG_AEC_PCM_HEX_DUMP
    if (data == nullptr || sample_count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if ((!armed_ && !started_) || !InitializeLocked() || capture_complete_ ||
            completed_ || dumping_) {
        return;
    }

    if (!started_) {
        if (channel != PcmDumpChannel::kAec || !playback_ready_ ||
                reference_hold_frames_ == 0) {
            return;
        }

        uint64_t level_sum = 0;
        for (size_t i = 0; i < sample_count; ++i) {
            const int32_t sample = data[i];
            level_sum += static_cast<uint32_t>(sample < 0 ? -sample : sample);
        }
        const uint32_t average_level = static_cast<uint32_t>(level_sum / sample_count);
        if (average_level < kAecTriggerAverage) {
            aec_candidate_frames_ = 0;
            return;
        }

        if (++aec_candidate_frames_ < kAecTriggerFrames) {
            return;
        }
        started_ = true;
        armed_ = false;
        ESP_LOGI(TAG, "Double-talk candidate detected (AEC avg=%u), PCM capture started",
                 static_cast<unsigned>(average_level));
    }

    auto& buffer = buffers_[static_cast<size_t>(channel)];
    const size_t copy_samples = std::min(sample_count,
        capacity_samples_ - buffer.sample_count);
    if (copy_samples > 0) {
        memcpy(buffer.data + buffer.sample_count, data,
               copy_samples * sizeof(int16_t));
        buffer.sample_count += copy_samples;
    }

    StartDumpTaskLocked();
#else
    (void)channel;
    (void)data;
    (void)sample_count;
#endif
}

void PcmHexDumper::CaptureInterleaved(PcmDumpChannel channel,
                                      const int16_t* data,
                                      size_t frame_count,
                                      size_t channel_count,
                                      size_t channel_index) {
#if CONFIG_AEC_PCM_HEX_DUMP
    if (data == nullptr || frame_count == 0 || channel_count == 0 ||
            channel_index >= channel_count) {
        return;
    }

    const int16_t* channel_data = data + channel_index;
    std::lock_guard<std::mutex> lock(mutex_);
    if ((!armed_ && !started_) || !InitializeLocked() || capture_complete_ ||
            completed_ || dumping_) {
        return;
    }

    if (!started_) {
        if (channel != PcmDumpChannel::kReference) {
            return;
        }

        if (HasReferenceSignal(channel_data, frame_count, channel_count)) {
            reference_hold_frames_ = kReferenceHoldFrames;
            if (!playback_ready_) {
                reference_signal_samples_ += frame_count;
                if (reference_signal_samples_ >= kReferenceWarmupSamples) {
                    playback_ready_ = true;
                    ESP_LOGI(TAG, "Sustained playback detected; waiting for double-talk");
                }
            }
        } else if (reference_hold_frames_ > 0) {
            --reference_hold_frames_;
        }
        return;
    }

    auto& buffer = buffers_[static_cast<size_t>(channel)];
    const size_t copy_samples = std::min(frame_count,
        capacity_samples_ - buffer.sample_count);
    for (size_t i = 0; i < copy_samples; ++i) {
        buffer.data[buffer.sample_count + i] = channel_data[i * channel_count];
    }
    buffer.sample_count += copy_samples;

    StartDumpTaskLocked();
#else
    (void)channel;
    (void)data;
    (void)frame_count;
    (void)channel_count;
    (void)channel_index;
#endif
}

void PcmHexDumper::StartDumpTaskLocked() {
#if CONFIG_AEC_PCM_HEX_DUMP
    if (!started_ || capture_complete_ || dumping_ || completed_) {
        return;
    }

    for (const auto& buffer : buffers_) {
        if (buffer.sample_count < capacity_samples_) {
            return;
        }
    }

    capture_complete_ = true;
    started_ = false;
    ESP_LOGW(TAG, "PCM capture complete; deferred hex output until idle");
#endif
}

void PcmHexDumper::DumpTaskEntry(void* arg) {
    static_cast<PcmHexDumper*>(arg)->Dump();
    vTaskDelete(nullptr);
}

void PcmHexDumper::Dump() {
#if CONFIG_AEC_PCM_HEX_DUMP
    static const char hex[] = "0123456789ABCDEF";

    ESP_LOGW(TAG, "PCM capture complete; hex output starts now");
    for (size_t channel = 0; channel < buffers_.size(); ++channel) {
        const auto& buffer = buffers_[channel];
        const size_t total_bytes = buffer.sample_count * sizeof(int16_t);

        printf("PCM_HEX_BEGIN|%s|rate=16000|format=S16LE|samples=%u|bytes=%u\n",
               kChannelNames[channel],
               static_cast<unsigned>(buffer.sample_count),
               static_cast<unsigned>(total_bytes));
    }

    const size_t total_bytes = buffers_[0].sample_count * sizeof(int16_t);
    for (size_t offset = 0; offset < total_bytes; offset += kBytesPerLine) {
        const size_t count = std::min(kBytesPerLine, total_bytes - offset);

        for (size_t channel = 0; channel < buffers_.size(); ++channel) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffers_[channel].data);
            char text[kBytesPerLine * 3];
            size_t pos = 0;

            for (size_t i = 0; i < count; ++i) {
                const uint8_t value = bytes[offset + i];
                text[pos++] = hex[value >> 4];
                text[pos++] = hex[value & 0x0F];
                if (i + 1 < count) {
                    text[pos++] = ' ';
                }
            }
            text[pos] = '\0';

            printf("PCM_HEX|%s|%u|%s\n", kChannelNames[channel],
                   static_cast<unsigned>(offset), text);
        }

        if ((offset / kBytesPerLine) % 16 == 15) {
            vTaskDelay(1);
        }
    }

    for (size_t channel = 0; channel < buffers_.size(); ++channel) {
        printf("PCM_HEX_END|%s\n", kChannelNames[channel]);
    }
    fflush(stdout);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ReleaseBuffersLocked();
        capture_complete_ = false;
        completed_ = true;
        dumping_ = false;
    }
    ESP_LOGW(TAG, "PCM hex output complete");
#endif
}

void PcmHexDumper::ReleaseBuffersLocked() {
#if CONFIG_AEC_PCM_HEX_DUMP
    for (auto& buffer : buffers_) {
        if (buffer.data != nullptr) {
            heap_caps_free(buffer.data);
            buffer.data = nullptr;
        }
        buffer.sample_count = 0;
    }
#endif
}
