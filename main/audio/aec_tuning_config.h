#ifndef AEC_TUNING_CONFIG_H
#define AEC_TUNING_CONFIG_H

#include <cstddef>

enum class AecProcessorMode {
    kSpeechRecognitionLowCost,
    kSpeechRecognitionHighPerformance,
    kVoiceCommunicationLowCost,
    kVoiceCommunicationHighPerformance,
    kFullDuplexLowCost,
    kFullDuplexHighPerformance,
};

enum class AecNlpLevel {
    kNormal,
    kAggressive,
    kVeryAggressive,
};

// Defaults used by boards that do not provide hardware-specific AEC tuning.
struct AecTuningConfig {
    AecProcessorMode processor_mode = AecProcessorMode::kFullDuplexHighPerformance;
    int filter_length = 4;
    AecNlpLevel nlp_level = AecNlpLevel::kNormal;
    bool agc_enabled = false;

    float input_gain_db = 30.0f;
    int software_reference_buffer_ms = 300;
    int software_reference_delay_ms = 0;
    int reference_burst_gap_ms = 120;

    size_t upload_gate_output_delay_frames = 2;
    double upload_gate_reference_active_rms = 250.0;
    double upload_gate_near_end_mic_rms = 180.0;
    double upload_gate_min_output_to_mic_ratio = 0.42;
    double upload_gate_min_mic_to_reference_ratio = 0.32;
    int upload_gate_near_end_hangover_frames = 3;
};

inline constexpr AecTuningConfig kDefaultAecTuningConfig{};

#endif  // AEC_TUNING_CONFIG_H
