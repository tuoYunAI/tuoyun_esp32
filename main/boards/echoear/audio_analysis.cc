#include "audio_analysis.h"
#include "audio_doa_app.h"
#include "application.h"
#include "board.h"
#include "audio_codec.h"
#include "display/emote_display.h"
#include "device_state.h"
#include <esp_log.h>
#include <cstring>
// #include "echo_base_control.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <wifi_station.h>

#define TAG "AudioAnalysis"

AudioAnalysis::AudioAnalysis() : beat_detection_handle_(nullptr), doa_app_handle_(nullptr)
{
}

AudioAnalysis::~AudioAnalysis()
{
    // Handles will be cleaned up by their respective components
}

void AudioAnalysis::Initialize()
{
    // AudioCodec* codec = Board::GetInstance().GetAudioCodec();
    // int channel_count = codec != nullptr ? codec->input_channels() : 1;
    // ESP_LOGI(TAG, "Initializing audio analysis with %d channels", channel_count);
    int channel_count = 2;

    beat_detection_cfg_t cfg = BEAT_DETECTION_DEFAULT_CFG();
    cfg.audio_cfg.channel = 2; // 2 channels
    cfg.result_callback = BeatDetectionResultCallback;
    cfg.result_callback_ctx = this;
    cfg.flags.enable_psram = false;

    esp_err_t ret = beat_detection_init(&cfg, &beat_detection_handle_);
    if (ret != ESP_OK || beat_detection_handle_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to initialize beat detection: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "Beat detection initialized successfully (channels: %d)", channel_count);
    }

    // Initialize audio DOA
    audio_doa_app_config_t doa_app_cfg = {
        .audio_doa_result_callback = DoaTrackerResultCallback,
        .audio_doa_result_callback_ctx = NULL,
    };
    ret = audio_doa_app_create(&doa_app_handle_, &doa_app_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create audio DOA app");
    }
    else
    {
        ESP_LOGI(TAG, "Audio DOA app created successfully");
    }
}

void AudioAnalysis::BeatDetectionResultCallback(beat_detection_result_t result, void *ctx)
{
    AudioAnalysis *self = static_cast<AudioAnalysis *>(ctx);
    if (self == nullptr)
    {
        return;
    }

    if (self->mode_ == AudioAnalysisMode::BEAT_DETECTION && result == BEAT_DETECTED)
    {
        ESP_LOGI(TAG, "Beat detected (result=%d), triggering beat swing action", result);
        // echo_base_control_set_action(ECHO_BASE_CMD_SET_ACTION_BEAT_SWING);

        Display *display = Board::GetInstance().GetDisplay();
        if (display != nullptr)
        {
            emote::EmoteDisplay *emote_display = dynamic_cast<emote::EmoteDisplay *>(display);
            if (emote_display != nullptr)
            {
                emote_display->InsertAnimDialog("beat_swing", 8 * 1000); // 8 seconds animation
            }
        }
    }
}

void AudioAnalysis::DoaTrackerResultCallback(float angle, void *ctx)
{
    ESP_LOGI(TAG, "Estimated direction: %.2f", angle);
    // echo_base_control_set_angle(angle);
}

void AudioAnalysis::SetAfeDataProcessCallback()
{
    auto &app = Application::GetInstance();
    app.GetAudioService().SetAfeDataProcessedCallback([this](const int16_t *audio_data, size_t total_bytes)
                                                      { OnAfeDataProcessed(audio_data, total_bytes); });
}

void AudioAnalysis::SetVadStateChangeCallback()
{
    auto &app = Application::GetInstance();
    app.GetAudioService().SetVadStateChangeCallback([this](bool speaking)
                                                    { OnVadStateChange(speaking); });
}

void AudioAnalysis::SetAudioDataProcessedCallback()
{
    auto &app = Application::GetInstance();
    app.GetAudioService().SetAudioDataProcessedCallback([this](const int16_t *audio_data, size_t bytes_per_channel, size_t channels)
                                                        { OnAudioDataProcessed(audio_data, bytes_per_channel, channels); });
}

void AudioAnalysis::OnAfeDataProcessed(const int16_t *audio_data, size_t total_bytes)
{
}

void AudioAnalysis::OnVadStateChange(bool speaking)
{
    if (speaking)
    {
        ESP_LOGD(TAG, "active");
        audio_doa_app_set_vad_detect(doa_app_handle_, true);
    }
    else
    {
        ESP_LOGD(TAG, "silence");
        audio_doa_app_set_vad_detect(doa_app_handle_, false);
    }
}

void AudioAnalysis::SetMode(AudioAnalysisMode mode)
{
    mode_ = mode;

    // Stop any existing animation dialog when mode changes
    Display *display = Board::GetInstance().GetDisplay();
    if (display != nullptr)
    {
        emote::EmoteDisplay *emote_display = dynamic_cast<emote::EmoteDisplay *>(display);
        if (emote_display != nullptr)
        {
            emote_display->StopAnimDialog();
        }
    }
    ESP_LOGI(TAG, "Audio analysis mode set to: %d", static_cast<int>(mode));
}

void AudioAnalysis::OnAudioDataProcessed(const int16_t *audio_data, size_t bytes_per_channel, size_t channels)
{
    // Incoming: bytes=1024, channels=2
    //  ESP_LOGI(TAG, "Incoming: bytes=%d, channels=%d", bytes_per_channel, channels);

    switch (mode_)
    {
    case AudioAnalysisMode::BEAT_DETECTION:
        // Feed to beat detection
        if (beat_detection_handle_ != nullptr)
        {
            beat_detection_audio_buffer_t buffer = {
                .audio_buffer = (uint8_t *)audio_data,
                .bytes_size = bytes_per_channel * channels,
            };
            beat_detection_data_write(beat_detection_handle_, buffer);
        }
        break;

    case AudioAnalysisMode::DOA_FOLLOW:
    {
        auto &app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateSpeaking)
        {
            break;
        }
        // Feed to audio DOA
        if (doa_app_handle_ != nullptr)
        {
            audio_doa_app_data_write(doa_app_handle_, (uint8_t *)audio_data, bytes_per_channel * channels);
        }
        break;
    }

    case AudioAnalysisMode::DISABLED:
    default:
        break;
    }
}
