#ifndef __SIP_ADAPTER_H__
#define __SIP_ADAPTER_H__


#include "sip.h"
#include <ctype.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * SIP消息缓存到列表中，并用任务进行异步处理
 * 强烈建议打开此宏，以异步方式处理收到的 SIP 消息，避免阻塞 MQTT 回调任务。
 * 否则在处理 SIP 消息时可能会阻塞 MQTT 客户端，导致不可预见的异常
 */
#define SIP_MESSAGE_CACHED_IN_LIST       
#define DCP_VERSION                            "2.1"
#define REGISTER_EXPIRE_SECOND                 300      //注册间隔5分钟   
#define COMMAND_TIMEOUT_MS                     30000         //命令超时时间
#define SESSION_SUPORT_MCP                     1       //是否支持MCP扩展
#define SESSION_OPUS_CBR                       0       //opus是否使用cbr编码
#define SESSION_AUDIO_FRAME_GAP                55      //音频帧间隔ms, 需要比frame_duration小
#define SESSION_SUPPORT_FRAME_AGGREGATION      0       //是否支持帧聚合，开启后如果服务端发送的帧将把3个帧放在一个UDP中下发，减少处理开销和丢包率，但会增加一点点编码延迟



typedef enum {
    LOG_DEBUG_LEVEL = 0,
    LOG_INFO_LEVEL = 1,
    LOG_WARN_LEVEL = 2,
    LOG_ERROR_LEVEL =3
} sip_log_level_t;

/**
 * @brief  Log message with specified level and tag
 */
void adatper_log(sip_log_level_t level, const char* tag, const char* format, ...);

#ifndef ADAPTER_LOG_TAG
#define ADAPTER_LOG_TAG "[SIP]"
#endif

#ifndef LOG_LEVEL_ENABLED
#define LOG_LEVEL_ENABLED LOG_INFO_LEVEL
#endif

#define LOG_DEBUG(fmt, ...) if (LOG_LEVEL_ENABLED == LOG_DEBUG_LEVEL) adatper_log(LOG_DEBUG_LEVEL, ADAPTER_LOG_TAG, fmt "\r\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  if (LOG_LEVEL_ENABLED <= LOG_INFO_LEVEL) adatper_log(LOG_INFO_LEVEL, ADAPTER_LOG_TAG, fmt "\r\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  if (LOG_LEVEL_ENABLED <= LOG_WARN_LEVEL) adatper_log(LOG_WARN_LEVEL, ADAPTER_LOG_TAG, fmt "\r\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) if (LOG_LEVEL_ENABLED <= LOG_ERROR_LEVEL) adatper_log(LOG_ERROR_LEVEL, ADAPTER_LOG_TAG, fmt "\r\n", ##__VA_ARGS__)



/**
 * @brief  Get system time in milliseconds
 */
uint32_t adapter_get_system_ms(void);

/**
 * @brief  Start a periodic task
 */
sip_ret_t adapter_start_periodic_task(void (*task_func)(void *), int period_ms, int stack_size, void* arg);

/**
 * @brief  Start a new thread
 */
sip_ret_t adapter_start_thread(void (*task_func)(void*), const char* name, int stack_size, int priority, void * const pvParameters);

/**
 * @brief  Delay current task for specified milliseconds
 */
void adapter_task_delay(int delay_ms);

/**
 * @brief  Create a new JSON object
 */
void* adapter_create_json_object();

/**
 * @brief  Create a new JSON array
 */
void* adapter_create_json_array();

/**
 * @brief  Create a new JSON integer object
 */
void* adapter_json_object_new_int(int value);

/**
 * @brief  Create a new JSON string object
 */
void* adapter_json_object_new_string(const char* value);

/**
 * @brief  Create a new JSON boolean object
 */
void* adapter_json_object_new_boolean(bool value);

/**
 * @brief  Add a JSON object to a JSON array
 */
void adapter_array_add_json_object(void* array, void* obj);

/**
 * @brief  Add a JSON object to a JSON object with a specified key
 */
void adapter_put_json_object_value(void* root, char* key, void* obj);

/**
 * @brief  Add a string value to a JSON object
 */
void* adapter_put_json_string_value(void* obj, const char* key, const char* value);


/**
 * @brief  Parse a JSON string into a JSON object
 */
void* adapter_parse_json_string(const char* json_str);


/**
 * @brief  Serialize a JSON object to a string
 */
char* adapter_serialize_json_to_string(void* obj);


/**
 * @brief  Get an integer value from a JSON object by key
 */
int adapter_get_json_int_value(void* obj, const char* key, int default_value);

/**
 * @brief  Get a string value from a JSON object by key
 */
char* adapter_get_json_string_value(void* obj, const char* key);

bool adapter_get_json_boolean_value(void* obj, const char* key, bool default_value);

/**
 * @brief  Get a JSON node from a JSON object by key
 */
void* adapter_get_json_node_value(void* obj, const char* key);

/**
 * @brief  Delete a JSON object
 */
void adapter_delete_json_object(void* obj);

/**
 * @brief  Lock SIP session mutex
 */
void adapter_lock_sip_mutex();

/**
 * @brief  Unlock SIP session mutex
 */
void adapter_unlock_sip_mutex();

/**
 * @brief  Lock SIP message list mutex
 */
void adapter_lock_sip_list_mutex();

/**
 * @brief  Unlock SIP message list mutex
 */
void adapter_unlock_sip_list_mutex();

/**
 * @brief  Lock MCP tools list mutex
 */
void adapter_lock_mcp_mutex();

/**
 * @brief  Unlock MCP tools list mutex
 */
void adapter_unlock_mcp_mutex();

/**
 * @brief  Start media traffic tunnel after call is established
 */
int adapter_start_traffic_tunnel(media_parameter_ptr media_param);

/**
 * @brief  Clear media traffic tunnel after call is terminated
 */ 
void adapter_clear_traffic_tunnel();


/**
 * @brief  Send message to server via MQTT
 * @param  message: Pointer to the message string to be sent
 * @return void
 */
void adapter_transmit_mqtt_message(char* message);


/**
 * @brief  Callback when call is established
 */
void on_call_established(char* session_id, media_parameter_ptr media_param);

/**
 * @brief  Callback when call ACK encounters error
 */
void on_call_ack_error(session_call_error_t error_code);


/**
 * @brief  Callback when call is terminated by server
 */
void on_call_terminated_by_server();

/**
 * @brief  Callback when call termination ACK is received
 */
void on_call_terminated_ack();

/**
 * @brief  Callback when server sends message notification
 */
void on_server_notify(event_system_notification_ptr notify);

/**
 * @brief  Callback when received a command from server to set device mode
 */
void on_set_device_mode(control_device_mode_set_ptr params);

/**
 * @brief  Callback when received a command from server to execute motion
 */
void on_execute_motion(control_device_motion_execute_ptr params);

/**
 * @brief  Callback when received a command from server to stop motion
 */
void on_stop_motion(control_device_motion_stop_ptr params);

/**
 * @brief  Callback when server sends lifecycle event notification, 
 * used to notify the application layer of the current lifecycle 
 * status of the device, such as activation, deactivation, etc.
 */
void on_server_lifecycle_event(event_device_lifecycle_ptr lifecycle);

/**
 * @brief  Callback when server sends session input text notification, 
 *  used to notify the application layer of the text content input audio.
 */
void on_server_session_input_text_notify(data_audio_input_text_ptr audio_input_text);

/**
 * @brief  Callback when server sends session output text notification,
 */
void on_server_session_output_text_notify(data_audio_output_text_ptr audio_output_text);

/**
 * @brief  Callback when server sends session update notification
 */ 
void on_server_session_update_notify(control_audio_output_state_ptr session_event);

/**
 * @brief  Callback when server sends MCP call message
 */
void on_server_mcp_call(const char* message);

int adapter_get_audio_volume();

#ifdef __cplusplus
}
#endif

#endif