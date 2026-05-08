#include <esp_log.h>
#include "sip_mqtt_protocol.h"
#include "sip.h"
#include "application.h"
#include <wifi_manager.h>
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "SIP-MQTT"

SipMqttProtocol::SipMqttProtocol() {
    
}

SipMqttProtocol::~SipMqttProtocol() {
    
}



bool SipMqttProtocol::Start(bool report_error) {
    bool ret = MqttProtocol::Start(report_error);
    if (!ret) {
        ESP_LOGW(TAG, "SipMqttProtocol failed to start MQTT client");
        return false;
    }

    Settings settings("mqtt", false);
    std::string username = settings.GetString("username");
    auto& wifi = WifiManager::GetInstance();
    std::string ip = wifi.GetIpAddress();
    init_session_module(username.c_str(), ip.c_str());

    Mqtt& mqtt = getMqtt();
    mqtt.OnMessage([this](const std::string& topic, const std::string& payload) {
        handle_received_mqtt_message(payload.c_str(), payload.length());
        last_incoming_time_ = std::chrono::steady_clock::now();
    });
    return true;
}



bool SipMqttProtocol::SendInitCall(const std::string& wakeWord){
    return (0 == init_call(wakeWord.c_str()));
}

void SipMqttProtocol::SendMcpMessage(const std::string& message)  {
    transmit_mcp_over_sip(message.c_str());
}

bool SipMqttProtocol::SendFinishCall(){

    return (0 == finish_call());
}


void SipMqttProtocol::SendAbortSpeaking(AbortReason reason) {
    event_session_barge_in_t param = {
        .reason = reason == kAbortReasonWakeWordDetected ? BARGE_IN_REASON_WAKE_WORD_DETECTED : BARGE_IN_REASON_NONE,
        .text = {0}
    };
    send_abort_speaking(&param);
}

void SipMqttProtocol::SendWakeWordDetected(const std::string& wake_word) {
    return;
}

void SipMqttProtocol::SendStartListening(ListeningMode mode) {
    listening_mode_t message;
    
    if (mode == kListeningModeRealtime) {
        message = LISTENING_MODE_REALTIME;
    } else if (mode == kListeningModeAutoStop) {
        message = LISTENING_MODE_AUTO_STOP;
    } else {
        message = LISTENING_MODE_MANUAL_STOP;
    }

    send_start_listening(message);
}

void SipMqttProtocol::SendStopListening() {
    send_stop_listening(AUDIO_INPUT_STOP_REASON_NONE);
}


void SipMqttProtocol::TransmitSIPMessage(const std::string& message) {
    SendText(message);
}

void SipMqttProtocol::ShowErrorMessage(const std::string& message) {
    SetError(message);
}

void SipMqttProtocol::OnCallEstablished(std::string sessionId,  media_parameter_ptr mediaParam) {
    auto& app = Application::GetInstance();
    session_id_ = sessionId;
    server_sample_rate_ = mediaParam->sample_rate;
    server_frame_duration_ = mediaParam->frame_duration;

    udp_server_ = std::string(mediaParam->ip);
    udp_port_ = mediaParam->port;
    aes_nonce_ = std::string((char*)mediaParam->nonce, 16);

    mbedtls_aes_init(&aes_ctx_);
    mbedtls_aes_setkey_enc(&aes_ctx_, (const unsigned char*)mediaParam->aes_key, 128);
    local_sequence_ = 0;
    remote_sequence_ = 0;

    if (app.GetDeviceState() == kDeviceStateConnecting) {
        xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);
        ESP_LOGI(TAG, "@@@@@@@@@@@@SIP call established,UDP server: %s, port: %d", udp_server_.c_str(), udp_port_);
    }else{
        ESP_LOGI(TAG, "@@@@@@@@@@@@SIP call established, now to open audio channel");
        OpenAudioChannel();
        app.Schedule([&app]() {
            app.SetDeviceState(kDeviceStateSpeaking);
        });
    }
    
}

void SipMqttProtocol::OnCallAckError(session_call_error_t error_code) {
    if (error_code == CALL_ERROR_MEMBERSHIP_INVALID){
        ESP_LOGW(TAG, "Received call ack error: membership invalid");
        ShowErrorMessage(Lang::Strings::DEVICE_MEMBERSHIP_EXPIRED);
        xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_ALERT_EVENT);
    }
}

void SipMqttProtocol::OnCallTerminatedByServer() {
    ESP_LOGI(TAG, "SIP call terminated by server");
    Application::GetInstance().Schedule([this]() {
        CloseAudioChannel(false);
    });
}

void SipMqttProtocol::OnCallTerminatedAck() {
    OnCallTerminatedByServer();
}
