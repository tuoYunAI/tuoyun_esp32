#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H


#include "protocol.h"
#include <mqtt.h>
#include <udp.h>
#include <cJSON.h>
#include <mbedtls/aes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>

#define MQTT_PING_INTERVAL_SECONDS 90
#define MQTT_RECONNECT_INTERVAL_MS 60000

#define DEVICE_STATUS_MEMBER_SHIP_EXPIRED 1
#define DEVICE_STATUS_ACTIVATED 2

#define MQTT_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)
#define MQTT_PROTOCOL_SERVER_ALERT_EVENT (1 << 1)



class MqttProtocol : public Protocol {
public:
    MqttProtocol();
    ~MqttProtocol();

    bool Start(bool report_error = false) override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel(const std::string& wakeWord = std::string("")) override;
    void CloseAudioChannel(bool notify_server = true) override;
    bool IsAudioChannelOpened() const override;
    Mqtt& getMqtt();
    virtual bool SendInitCall(const std::string& wakeWord);
    virtual bool SendFinishCall();
protected:
    bool SendText(const std::string& text) override;

    mbedtls_aes_context aes_ctx_;
    std::string aes_nonce_;
    std::string udp_server_;
    int udp_port_;
    uint32_t local_sequence_;
    uint32_t remote_sequence_;
    
    EventGroupHandle_t event_group_handle_;

private:

    // Alive flag for safe scheduled callbacks - set to false in destructor
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    
    std::string publish_topic_;

    std::mutex channel_mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    std::unique_ptr<Udp> udp_;
    
    esp_timer_handle_t reconnect_timer_;

    bool StartMqttClient(bool report_error=false);
    void ParseServerHello(const cJSON* root);
    std::string DecodeHexString(const std::string& hex_string);
    std::string GetHelloMessage();
};


#endif // MQTT_PROTOCOL_H
