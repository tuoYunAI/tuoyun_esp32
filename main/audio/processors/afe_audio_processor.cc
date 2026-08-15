#include "afe_audio_processor.h"
#include "pcm_hex_dumper.h"
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdint>

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

namespace {

aec_mode_t ToEspAecMode(AecProcessorMode mode) {
    switch (mode) {
        case AecProcessorMode::kSpeechRecognitionLowCost:
            return AEC_MODE_SR_LOW_COST;
        case AecProcessorMode::kSpeechRecognitionHighPerformance:
            return AEC_MODE_SR_HIGH_PERF;
        case AecProcessorMode::kVoiceCommunicationLowCost:
            return AEC_MODE_VOIP_LOW_COST;
        case AecProcessorMode::kVoiceCommunicationHighPerformance:
            return AEC_MODE_VOIP_HIGH_PERF;
        case AecProcessorMode::kFullDuplexLowCost:
            return AEC_MODE_FD_LOW_COST;
        case AecProcessorMode::kFullDuplexHighPerformance:
            return AEC_MODE_FD_HIGH_PERF;
    }
    return AEC_MODE_FD_HIGH_PERF;
}

aec_nlp_level_t ToEspAecNlpLevel(AecNlpLevel level) {
    switch (level) {
        case AecNlpLevel::kNormal:
            return AEC_NLP_LEVEL_NORMAL;
        case AecNlpLevel::kAggressive:
            return AEC_NLP_LEVEL_AGGR;
        case AecNlpLevel::kVeryAggressive:
            return AEC_NLP_LEVEL_VERYAGGR;
    }
    return AEC_NLP_LEVEL_NORMAL;
}

double Square(double value) {
    return value * value;
}

}  // namespace

AfeAudioProcessor::AfeAudioProcessor()
    : afe_data_(nullptr) {
    event_group_ = xEventGroupCreate();
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    aec_tuning_config_ = codec_->aec_tuning_config();
    frame_samples_ = frame_duration_ms * 16000 / 1000;
    input_channels_ = codec_->input_channels();
    has_reference_ = codec_->input_reference();

    // Pre-allocate output buffer capacity
    output_buffer_.reserve(frame_samples_);

    int ref_num = codec_->input_reference() ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    srmodel_list_t *models;
    if (models_list == nullptr) {
        models = esp_srmodel_init("model");
    } else {
        models = models_list;
    }

    char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
    
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = ToEspAecMode(aec_tuning_config_.processor_mode);
    afe_config->aec_filter_length = aec_tuning_config_.filter_length;
    // Preserve near-end speaker characteristics during double-talk. The
    // calibrated reference lets the linear AEC carry most echo suppression.
    afe_config->aec_nlp_level = ToEspAecNlpLevel(aec_tuning_config_.nlp_level);
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
    }

    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        afe_config->ns_init = false;
    }

    afe_config->agc_init = aec_tuning_config_.agc_enabled;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;
#else
    afe_config->aec_init = false;
    afe_config->vad_init = true;
#endif

#if CONFIG_AEC_PCM_HEX_DUMP
    ESP_LOGI(TAG,
        "Initializing AFE: input_format=%s, aec=%s, mode=%d, filter_length=%d, nlp_level=%d",
        input_format.c_str(), afe_config->aec_init ? "enabled" : "disabled",
        static_cast<int>(afe_config->aec_mode), afe_config->aec_filter_length,
        static_cast<int>(afe_config->aec_nlp_level));
#endif

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    
    xTaskCreate([](void* arg) {
        auto this_ = (AfeAudioProcessor*)arg;
        this_->AudioProcessorTask();
        vTaskDelete(NULL);
    }, "audio_communication", 4096, this, 3, NULL);
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    vEventGroupDelete(event_group_);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (afe_data_ == nullptr) {
        return;
    }

#if CONFIG_AEC_PCM_HEX_DUMP
    if (!data.empty() && input_channels_ > 0) {
        const size_t frame_count = data.size() / input_channels_;
        auto& dumper = PcmHexDumper::GetInstance();
        // Reference is captured first because it triggers the synchronized dump.
        if (has_reference_) {
            dumper.CaptureInterleaved(PcmDumpChannel::kReference, data.data(),
                frame_count, input_channels_, input_channels_ - 1);
        }
        dumper.CaptureInterleaved(PcmDumpChannel::kMic, data.data(),
            frame_count, input_channels_, 0);
    }
#endif

    if (has_reference_ && !data.empty()) {
        TrackInputMetrics(data);
    }

#if CONFIG_AEC_PCM_HEX_DUMP
    const int64_t now = esp_timer_get_time();
    if (!data.empty() && now - last_input_level_log_us_ >= 1000000) {
        uint32_t mic_peak = 0;
        uint64_t mic_sum = 0;
        uint32_t ref_peak = 0;
        uint64_t ref_sum = 0;
        size_t frames = 0;

        for (size_t i = 0; i + input_channels_ <= data.size(); i += input_channels_) {
            const uint32_t mic_level = static_cast<uint32_t>(std::abs(static_cast<int32_t>(data[i])));
            mic_peak = std::max(mic_peak, mic_level);
            mic_sum += mic_level;
            if (has_reference_) {
                const uint32_t ref_level = static_cast<uint32_t>(
                    std::abs(static_cast<int32_t>(data[i + input_channels_ - 1])));
                ref_peak = std::max(ref_peak, ref_level);
                ref_sum += ref_level;
            }
            ++frames;
        }

        if (frames > 0) {
            if (has_reference_) {
                ESP_LOGI(TAG, "AFE input levels: M peak=%u avg=%u, R peak=%u avg=%u",
                    static_cast<unsigned>(mic_peak), static_cast<unsigned>(mic_sum / frames),
                    static_cast<unsigned>(ref_peak), static_cast<unsigned>(ref_sum / frames));
            } else {
                ESP_LOGI(TAG, "AFE input level: M peak=%u avg=%u",
                    static_cast<unsigned>(mic_peak), static_cast<unsigned>(mic_sum / frames));
            }
        }
        last_input_level_log_us_ = now;
    }
#endif

    afe_iface_->feed(afe_data_, data.data());
}

void AfeAudioProcessor::Start() {
    ResetEchoGate();
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    ResetEchoGate();
}

bool AfeAudioProcessor::IsRunning() {
    return xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

void AfeAudioProcessor::AudioProcessorTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio communication task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if ((xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            if (res != nullptr) {
                ESP_LOGI(TAG, "Error code: %d", res->ret_value);
            }
            continue;
        }

#if CONFIG_AEC_PCM_HEX_DUMP
        if (res->data != nullptr && res->data_size > 0) {
            PcmHexDumper::GetInstance().Capture(PcmDumpChannel::kAec,
                res->data, res->data_size / sizeof(int16_t));
        }
#endif

#if CONFIG_AEC_PCM_HEX_DUMP
        const int64_t now = esp_timer_get_time();
        if (res->data != nullptr && res->data_size > 0 &&
                now - last_output_level_log_us_ >= 1000000) {
            const size_t samples = res->data_size / sizeof(int16_t);
            uint32_t peak = 0;
            uint64_t sum = 0;
            for (size_t i = 0; i < samples; ++i) {
                const uint32_t level = static_cast<uint32_t>(
                    std::abs(static_cast<int32_t>(res->data[i])));
                peak = std::max(peak, level);
                sum += level;
            }
            ESP_LOGI(TAG, "AFE output level: peak=%u avg=%u",
                static_cast<unsigned>(peak), static_cast<unsigned>(sum / samples));
            last_output_level_log_us_ = now;
        }
#endif

        // VAD state change
        if (vad_state_change_callback_) {
            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                vad_state_change_callback_(true);
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                vad_state_change_callback_(false);
            }
        }

        const size_t samples = res->data_size / sizeof(int16_t);
        const EchoGateDecision gate_decision = ClassifyAecOutput(res->data, samples);

        if (output_callback_) {
            if (gate_decision == EchoGateDecision::kSuppress) {
                output_buffer_.insert(output_buffer_.end(), samples, 0);
#if CONFIG_AEC_PCM_HEX_DUMP
                ++echo_gate_suppressed_frames_;
#endif
            } else {
                output_buffer_.insert(output_buffer_.end(), res->data, res->data + samples);
#if CONFIG_AEC_PCM_HEX_DUMP
                if (gate_decision == EchoGateDecision::kNearEnd) {
                    ++echo_gate_near_end_frames_;
                } else {
                    ++echo_gate_bypass_frames_;
                }
#endif
            }

#if CONFIG_AEC_PCM_HEX_DUMP
            if (now - last_echo_gate_log_us_ >= 1000000) {
                ESP_LOGI(TAG, "AEC upload gate: suppressed=%u near_end=%u bypass=%u",
                    static_cast<unsigned>(echo_gate_suppressed_frames_),
                    static_cast<unsigned>(echo_gate_near_end_frames_),
                    static_cast<unsigned>(echo_gate_bypass_frames_));
                echo_gate_suppressed_frames_ = 0;
                echo_gate_near_end_frames_ = 0;
                echo_gate_bypass_frames_ = 0;
                last_echo_gate_log_us_ = now;
            }
#endif

            // Output complete frames when buffer has enough data
            while (output_buffer_.size() >= frame_samples_) {
                if (output_buffer_.size() == frame_samples_) {
                    // If buffer size equals frame size, move the entire buffer
                    output_callback_(std::move(output_buffer_));
                    output_buffer_.clear();
                    output_buffer_.reserve(frame_samples_);
                } else {
                    // If buffer size exceeds frame size, copy one frame and remove it
                    output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
                    output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
                }
            }
        }
    }
}

void AfeAudioProcessor::TrackInputMetrics(const std::vector<int16_t>& data) {
    InputFrameMetrics metrics;
    metrics.samples = data.size() / input_channels_;

    for (size_t i = 0; i + input_channels_ <= data.size(); i += input_channels_) {
        const int32_t mic = data[i];
        const int32_t ref = data[i + input_channels_ - 1];
        metrics.mic_energy += static_cast<uint64_t>(mic * mic);
        metrics.ref_energy += static_cast<uint64_t>(ref * ref);
    }

    std::lock_guard<std::mutex> lock(input_metrics_mutex_);
    input_metrics_.push_back(metrics);
}

AfeAudioProcessor::EchoGateDecision AfeAudioProcessor::ClassifyAecOutput(
        const int16_t* data, size_t samples) {
    if (!has_reference_ || data == nullptr || samples == 0) {
        return EchoGateDecision::kBypass;
    }

    uint64_t output_energy = 0;
    for (size_t i = 0; i < samples; ++i) {
        const int32_t sample = data[i];
        output_energy += static_cast<uint64_t>(sample * sample);
    }

    std::lock_guard<std::mutex> lock(input_metrics_mutex_);
    if (input_metrics_.size() <= aec_tuning_config_.upload_gate_output_delay_frames) {
        return EchoGateDecision::kBypass;
    }

    const InputFrameMetrics metrics = input_metrics_.front();
    input_metrics_.pop_front();
    if (metrics.samples == 0) {
        return EchoGateDecision::kBypass;
    }

    const double mic_mean_energy = static_cast<double>(metrics.mic_energy) / metrics.samples;
    const double ref_mean_energy = static_cast<double>(metrics.ref_energy) / metrics.samples;
    const double output_mean_energy = static_cast<double>(output_energy) / samples;
    const bool near_end =
        mic_mean_energy >= Square(aec_tuning_config_.upload_gate_near_end_mic_rms) &&
        output_mean_energy >= mic_mean_energy *
            Square(aec_tuning_config_.upload_gate_min_output_to_mic_ratio) &&
        mic_mean_energy >= ref_mean_energy *
            Square(aec_tuning_config_.upload_gate_min_mic_to_reference_ratio);

    if (near_end) {
        near_end_hangover_frames_ =
            std::max(0, aec_tuning_config_.upload_gate_near_end_hangover_frames);
        return EchoGateDecision::kNearEnd;
    }
    if (near_end_hangover_frames_ > 0) {
        --near_end_hangover_frames_;
        return EchoGateDecision::kNearEnd;
    }
    if (ref_mean_energy >= Square(aec_tuning_config_.upload_gate_reference_active_rms)) {
        return EchoGateDecision::kSuppress;
    }
    return EchoGateDecision::kBypass;
}

void AfeAudioProcessor::ResetEchoGate() {
    std::lock_guard<std::mutex> lock(input_metrics_mutex_);
    input_metrics_.clear();
    near_end_hangover_frames_ = 0;
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
#else
        ESP_LOGE(TAG, "Device AEC is not supported");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
