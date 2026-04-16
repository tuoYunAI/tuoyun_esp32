#ifndef __SIP_PROTOCOL_H__
#define __SIP_PROTOCOL_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 用来表示被修饰的指针必须使用malloc申请, 将由函数移动所有权（即函数内部会释放该指针），
 * 调用者在调用后不应再使用该指针。
 */
#define MOVE

typedef enum {
    RET_OK = 0,
    RET_ERROR = 1
}sip_ret_t;


typedef enum {
    OFF = 0,
    ON = 1
} work_status_t;


/**
 * Device Registration parameters, including network status and battery status, etc. The device needs 
 * to report these parameters to the server when registering and when the parameters change during operation
 */

typedef enum {
    WIFI = 0,
    MOBILE_4G = 1,
    ETHERNET = 2
} network_type_t;

typedef struct {
    network_type_t type;
    int rssi; // signal strength, unit: dBm
} network_t, *network_ptr;

typedef struct{

    int online; // 1: online, 0: offline
    int battery; // battery level percentage, 0-100
    network_t network;
}register_param_t, *register_param_ptr;


 /**
 * Device session error codes
 */
typedef enum {
    /**
     * The server did not answer
     */
    CALL_ERROR_SERVER_NO_ANSWER = 1000,
    /**
     * Device not authorized
     */
    CALL_ERROR_MEMBERSHIP_INVALID = 1001
} session_call_error_t;



/**
 * Device control message notification event parameters from the server
 */
typedef struct {
    session_call_error_t event;
    char message[64];
}session_call_error_event_t, *session_call_error_event_ptr;



/**
 * DCP(Device control protocol) command types, including commands for both device control 
 * messages sent by the server and messages sent by the device to the server
 */

typedef enum {
    EVENT_REGISTER = 0,
    EVENT_AUDIO_INPUT_STATE = 1,
    EVENT_SESSION_BARGE_IN = 2,
    EVENT_SYSTEM_NOTIFICATION = 3,
    EVENT_DEVICE_LIFECYCLE = 4,

    
    DATA_AUDIO_INPUT_TEXT = 101,
    DATA_AUDIO_OUTPUT_TEXT = 102,

    CONTROL_AUDIO_OUTPUT_STATE = 201,
    CONTROL_DEVICE_MODE_SET = 202,
    CONTROL_DEVICE_MOTION_EXECUTE = 203,
    CONTROL_DEVICE_MOTION_STOP = 204,
    INVALID_CMD = 10000
} dcp_cmd_type_t;



/**
 * DCP Audio input state management.
 */

/**
 * Listening modes. The device can send start listening command to server with different 
 * listening modes according to different scenarios
 * Auto: The server will automatically determine when to stop listening based on VAD detection.
 * Manual: The device will keep listening until the device sends a stop listening command
 */
typedef enum  {
    LISTENING_MODE_AUTO_STOP = 0,
    LISTENING_MODE_MANUAL_STOP,
    LISTENING_MODE_REALTIME // 需要 AEC 支持
} listening_mode_t;

typedef enum{
    AUDIO_INPUT_STOP_REASON_NONE = 0,
    AUDIO_INPUT_STOP_REASON_VAD = 1,
    AUDIO_INPUT_STOP_REASON_MANUAL = 2,
    AUDIO_INPUT_STOP_REASON_ERROR = 3
} audio_input_stop_reason_t;

typedef struct {
    listening_mode_t mode;
} event_audio_input_state_start_t, *event_audio_input_state_start_ptr;

typedef struct {
    audio_input_stop_reason_t reason;
} event_audio_input_state_stop_t, *event_audio_input_state_stop_ptr;

typedef struct{
    char text[1024];
} data_audio_input_text_t, *data_audio_input_text_ptr;



/**
 * DCP audio output and text output management.
 */

typedef struct{
    work_status_t state;
} control_audio_output_state_t, *control_audio_output_state_ptr;

 typedef struct{
    char text[1024];
    char emotion[32];
} data_audio_output_text_t, *data_audio_output_text_ptr;    




/**
 * DCP session barge-in event
 */
typedef enum{
    BARGE_IN_REASON_NONE = 0,
    BARGE_IN_REASON_WAKE_WORD_DETECTED = 1
} barge_in_reason_t;

typedef struct{
    barge_in_reason_t reason;
    char text[20];
} event_session_barge_in_t, *event_session_barge_in_ptr;


/**
 * DCP system notification event
 */

 typedef enum{
    _INFO_ = 0,
    _WARNING_ = 1,
    _CRITICAL_ = 2   
} system_notification_level_t;

typedef struct{
    system_notification_level_t level;
    char emotion[32];
    char message[256];
} event_system_notification_t, *event_system_notification_ptr;  


/**
 * DCP device lifecycle event   
 */

 typedef enum{
    ACTIVE = 0,
    EXIPIRED = 1,
    TRIAL = 2,
    BLOCKED = 3
 }subscription_status_t;

typedef struct{
    int chat;
    int vision;
}device_capability_t, *device_capability_ptr;

typedef struct{
    int activated; // 1: activated, 0: not activated
    subscription_status_t subscription;
    device_capability_t capabilities;
}event_device_lifecycle_t, *event_device_lifecycle_ptr;


/**
 * DCP device mode setting control command
 */
typedef enum{
    EXPLANATION,
    INTERACTION,
    DUO,
    HUMAN_AGENT
}device_working_mode_t;

typedef struct{
    device_working_mode_t mode;
}control_device_mode_set_t, *control_device_mode_set_ptr;


/**
 * DCP device motion execute control command
 */
typedef enum{
    NOD = 0,
    SHAKE_HEAD = 1,
    DANCE = 2,
    WAVE = 3,
    EMOTION = 4,
    CUSTOM = 99,
    IDLE = 100
}device_motion_t;

typedef enum{
    _LOW_ = 0,
    _NORMAL_ = 1,
    _HIGH_ = 2,
    _INTERRUPT_ = 3 // Interrupt current motion and execute this motion immediately
}device_motion_priority_t;

typedef struct{
    device_motion_t action;
    device_motion_priority_t priority;
    int repeat; // Number of times to repeat the motion, 0 means no repeat, -1 means repeat indefinitely until interrupted
    float speed; // Speed of the motion, 0.5 means half speed, 1 means normal speed, 2 means double speed, etc.
}control_device_motion_execute_t, *control_device_motion_execute_ptr;

/**
 * DCP device motion stop control command
 */
typedef enum{
    ALL = 0,
    CURRENT = 1,
    TYPE = 2  // stop specific type of motion, need to specify the type in the command parameters
}device_motion_stop_scope_t;    

typedef struct{
    device_motion_stop_scope_t scope;
    device_motion_t type; // valid when scope is TYPE
}control_device_motion_stop_t, *control_device_motion_stop_ptr;




/**
 * Media parameters
 * Including audio codec parameters and encryption parameters
 * When the session is initiated, the device needs to negotiate media parameters with the server
 */
typedef struct{
    char ip[16];
    int port;
    char codec[16]; // "opus"
    char transport[8]; // "udp"
    int sample_rate; // 24000
    int channels; // 1
    int frame_duration; // 60 ms
    char encryption[16]; // "aes-128-cbc"
    unsigned char nonce[16];   // AES 初始向量/nonce
    unsigned char aes_key[16]; // AES 128位密钥
    int idle_timeout; // seconds of idle before terminates the session
}media_parameter_t, *media_parameter_ptr;



/**
 * @brief  Initialize session
 * @param  wake_up_word: Wake-up word string; if wake-up word detection is not needed, pass NULL.
 * @return RET_OK: Initialization successful
 *     Other: Initialization failed
 */
sip_ret_t init_call(const char* wake_up_word);



/**
 * @brief  End current session
 * @return 0: End successful
 *     Other: End failed
 */
sip_ret_t finish_call();


/**
 * @brief  Send start listening command to server
 * @return RET_OK: Sent successfully
 *     Other: Send failed
 */
sip_ret_t send_start_listening(listening_mode_t mode);

/**
 * @brief  Send stop listening command to server
 * @return RET_OK: Sent successfully
 *     Other: Send failed
 */
sip_ret_t send_stop_listening(audio_input_stop_reason_t reason);

/**
 * @brief  Send abort current speech synthesis command to server
 * @param  reason: Abort reason
 * @return RET_OK: Sent successfully
 *     Other: Sent failed
 */
sip_ret_t send_abort_speaking(event_session_barge_in_ptr reason);


/**
 * @brief  Initialize session module
 * @param  uid: Device UID
 * @param  device_ip: Device IP address
 */
void init_session_module(const char* uid, const char* device_ip);


/**
 * @brief  Handle received MQTT message containing SIP data
 * @param  data: Pointer to the received data
 * @param  len: Length of the received data
 */
void handle_received_mqtt_message(const char *data, size_t len);     

/**
 * @brief  Transmit MCP message over SIP
 * @param  message: Pointer to the MCP message string
 * @return 0: Transmission successful
 *     Other: Transmission failed
 */
sip_ret_t transmit_mcp_over_sip(const char *message);

/**
 * report the device status to session module, session module will report the status to server when needed, 
 * such as when sending REGISTER message
 */
void report_register_status(register_param_ptr  param);
/**
 * @brief  report that traffic tunnel is active
 * @return void
 */
void report_traffic_active();

void test_print_session_state();

#ifdef __cplusplus
}
#endif

#endif