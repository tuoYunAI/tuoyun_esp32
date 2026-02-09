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

#define REGISTER_EXPIRE_SECOND   300      //注册间隔5分钟   
#define COMMAND_TIMEOUT_MS       30000         //命令超时时间
#define SESSION_SUPORT_MCP       1       //是否支持MCP扩展
#define SESSION_OPUS_CBR         0            //opus是否使用cbr编码
#define SESSION_AUDIO_FRAME_GAP  60           //音频帧间隔ms



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
void adapter_start_thread(void (*task_func)(void*), const char* name, int stack_size, int priority);

/**
 * @brief  Delay current task for specified milliseconds
 */
void adapter_task_delay(int delay_ms);

/**
 * @brief  Create a new JSON object
 */
void* adapter_create_json_object();


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
 * @brief  Get a string value from a JSON object by key
 */
char* adapter_get_json_string_value(void* obj, const char* key);

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
void on_server_message_notify(server_message_notify_ptr notify);

/**
 * @brief  Callback when server sends session update notification
 */ 
void on_server_session_update_notify(message_session_event_ptr session_event);

/**
 * @brief  Callback when server sends MCP call message
 */
void on_server_mcp_call(const char* message);


#ifdef __cplusplus
}
#endif

#endif