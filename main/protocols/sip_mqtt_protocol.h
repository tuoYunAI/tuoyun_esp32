#ifndef SIP_MQTT_PROTOCOL_H
#define SIP_MQTT_PROTOCOL_H
#include "mqtt_protocol.h"
#include "adapter/protocol.h"


class SipMqttProtocol : public MqttProtocol {
public:
    SipMqttProtocol();
    ~SipMqttProtocol();


    bool Start(bool report_error = false) override;
    void OnCallEstablished(std::string sessionId,  media_parameter_ptr mediaParam);
    void OnCallAckError(session_call_error_t error_code);
    void OnCallTerminatedByServer();
    void OnCallTerminatedAck();
    virtual bool SendInitCall(const std::string& wakeWord) override;
    virtual void SendMcpMessage(const std::string& message) override;
    bool SendFinishCall() override;
    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;
    void TransmitSIPMessage(const std::string& message);
    void ShowErrorMessage(const std::string& message);
};



#endif