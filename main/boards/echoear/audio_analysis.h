#ifndef AUDIO_ANALYSIS_H
#define AUDIO_ANALYSIS_H

#include "beat_detection.h"
#include "beat_detection_config.h"
#include "audio_doa_app.h"
#include <sys/socket.h>
#include <netinet/in.h>

enum class AudioAnalysisMode {
    BEAT_DETECTION,  // 鼓点检测，跟着音乐跳舞
    DOA_FOLLOW,      // DOA 人声音跟随模式
    DISABLED         // 禁用
};

class AudioAnalysis {
public:
    AudioAnalysis();
    ~AudioAnalysis();

    void Initialize();

    // Callback setup methods
    void SetAfeDataProcessCallback();
    void SetVadStateChangeCallback();
    void SetAudioDataProcessedCallback();

    // Mode control
    void SetMode(AudioAnalysisMode mode);
    AudioAnalysisMode GetMode() const
    {
        return mode_;
    }

    // Beat detection
    beat_detection_handle_t GetBeatDetectionHandle() const
    {
        return beat_detection_handle_;
    }

    // Audio DOA
    audio_doa_app_handle_t GetDoaApp()
    {
        return doa_app_handle_;
    }

private:
    // Mode
    AudioAnalysisMode mode_ = AudioAnalysisMode::DOA_FOLLOW;

    // Beat detection
    beat_detection_handle_t beat_detection_handle_;
    static void BeatDetectionResultCallback(beat_detection_result_t result, void *ctx);

    // Audio DOA
    audio_doa_app_handle_t doa_app_handle_;
    static void DoaTrackerResultCallback(float angle, void *ctx);

    // Internal handlers
    void OnAfeDataProcessed(const int16_t* audio_data, size_t total_bytes);
    void OnVadStateChange(bool speaking);
    void OnAudioDataProcessed(const int16_t* audio_data, size_t bytes_per_channel, size_t channels);
};

#endif // AUDIO_ANALYSIS_H
