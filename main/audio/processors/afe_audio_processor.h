#ifndef AFE_AUDIO_PROCESSOR_H
#define AFE_AUDIO_PROCESSOR_H

#include <esp_afe_sr_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "audio_processor.h"
#include "audio_codec.h"

class AfeAudioProcessor : public AudioProcessor {
public:
    AfeAudioProcessor();
    ~AfeAudioProcessor();

    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override;
    void Feed(std::vector<int16_t>&& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    struct InputFrameMetrics {
        uint64_t mic_energy = 0;
        uint64_t ref_energy = 0;
        size_t samples = 0;
    };

    enum class EchoGateDecision {
        kBypass,
        kNearEnd,
        kSuppress,
    };

    EventGroupHandle_t event_group_ = nullptr;
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    AudioCodec* codec_ = nullptr;
    AecTuningConfig aec_tuning_config_ = kDefaultAecTuningConfig;
    int frame_samples_ = 0;
    int input_channels_ = 1;
    bool has_reference_ = false;
#if CONFIG_AEC_PCM_HEX_DUMP
    int64_t last_input_level_log_us_ = 0;
    int64_t last_output_level_log_us_ = 0;
    int64_t last_echo_gate_log_us_ = 0;
#endif
    bool is_speaking_ = false;
    std::vector<int16_t> output_buffer_;
    std::mutex input_metrics_mutex_;
    std::deque<InputFrameMetrics> input_metrics_;
    int near_end_hangover_frames_ = 0;
#if CONFIG_AEC_PCM_HEX_DUMP
    uint32_t echo_gate_suppressed_frames_ = 0;
    uint32_t echo_gate_near_end_frames_ = 0;
    uint32_t echo_gate_bypass_frames_ = 0;
#endif

    void AudioProcessorTask();
    void TrackInputMetrics(const std::vector<int16_t>& data);
    EchoGateDecision ClassifyAecOutput(const int16_t* data, size_t samples);
    void ResetEchoGate();
};

#endif
