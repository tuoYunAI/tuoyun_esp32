#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <cJSON.h>
#include <cstdarg>
#include "application.h"
#include "assets/lang_config.h"
#include "sip_mqtt_protocol.h"

#include <esp_log.h>
#include <esp_heap_caps.h>

#define TAG                "[SIP-ADAPTER]"
#define LOG_LEVEL_ENABLED  LOG_INFO_LEVEL
#include "adapter.h"



void adatper_log(sip_log_level_t level, const char* tag, const char* format, ...){
    esp_log_level_t log_level = ESP_LOG_INFO;
    switch (level) {
    case LOG_DEBUG_LEVEL:
        log_level = ESP_LOG_DEBUG;
        break;
    case LOG_INFO_LEVEL:
        log_level = ESP_LOG_INFO;
        break;
    case LOG_WARN_LEVEL:
        log_level = ESP_LOG_WARN;
        break;
    case LOG_ERROR_LEVEL:
        log_level = ESP_LOG_ERROR;
        break;  
    default:
        log_level = ESP_LOG_NONE;
    }

    esp_log_config_t configs = {
        .opts = {
            .log_level = log_level,                 // Set log level
            .require_formatting = true,                // Enable formatting
            .dis_color = ESP_LOG_COLOR_DISABLED,       // Use global color setting
            .dis_timestamp = CONFIG_LOG_TIMESTAMP_SOURCE_RTOS, // Use global timestamp setting
            .reserved = 0,                             // Reserved for future use
        }
    };

    va_list args;
    va_start(args, format);
    esp_log_va(configs, tag, format, args);
    va_end(args);
}


uint32_t adapter_get_system_ms(void){

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
    return time_us / 1000;
}



void adapter_start_thread(void (*task_func)(void*), const char* name, int stack_size, int priority){
    StackType_t *task_stack = (StackType_t *)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
    StaticTask_t *task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (task_stack && task_tcb) {
        xTaskCreateStatic(task_func, name ? name : "poll", stack_size, NULL, priority, task_stack, task_tcb);
    } else {
        if (task_stack) free(task_stack);
        if (task_tcb) free(task_tcb);
        LOG_INFO("@@@@@@@@@@@@@@@@@Failed to allocate stack in PSRAM for %s, falling back to internal", name ? name : "poll");
        xTaskCreate(
            task_func,
            name ? name:"poll",
            stack_size,
            NULL,
            priority,
            NULL
        );
    }
}

/**
 * 以下封装定时任务的启动函数
 */
typedef void (*timer_task_cb_t)(void *arg);
typedef struct
{
    TickType_t       period_ticks;
    timer_task_cb_t  callback;
    void            *arg;
} timer_task_ctx_t;

static void timer_task_entry(void *param)
{
    timer_task_ctx_t *ctx = (timer_task_ctx_t *)param;
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        ctx->callback(ctx->arg);
        vTaskDelayUntil(&lastWakeTime, ctx->period_ticks);
    }
}

sip_ret_t adapter_start_periodic_task(void (*timer_task_cb)(void*), int period_ms, int stack_size, void* arg){
    static timer_task_ctx_t ctx_pool[8];
    static uint8_t index = 0;

    if (index >= 8 || timer_task_cb == NULL)
        return RET_ERROR;

    timer_task_ctx_t *ctx = &ctx_pool[index];
    ctx->period_ticks = pdMS_TO_TICKS(period_ms);
    ctx->callback     = timer_task_cb;
    ctx->arg          = arg;
    std::string name = "timer_task_" + std::to_string(index++);

    StackType_t *task_stack = (StackType_t *)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
    StaticTask_t *task_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (task_stack && task_tcb) {
        return xTaskCreateStatic(
            timer_task_entry,
            name.c_str(),
            stack_size,
            ctx,
            tskIDLE_PRIORITY + 5,
            task_stack,
            task_tcb
        ) != NULL ? RET_OK : RET_ERROR;
    } else {
        if (task_stack) free(task_stack);
        if (task_tcb) free(task_tcb);
        LOG_WARN("@@@@@@@@@@@@@@@Failed to allocate stack in PSRAM for %s, falling back to internal", name.c_str());
        return xTaskCreate(
            timer_task_entry,
            name.c_str(),
            stack_size,
            ctx,
            tskIDLE_PRIORITY + 5,
            NULL
        ) == pdPASS ? RET_OK : RET_ERROR;
    }


}



void adapter_task_delay(int delay_ms){
    TickType_t lastWakeTime = xTaskGetTickCount();;
    const TickType_t period = pdMS_TO_TICKS(delay_ms);  // 100ms 轮询周期

    /* ===== 等待下一个周期（不漂移） ===== */
    vTaskDelayUntil(&lastWakeTime, period);
}

void* adapter_create_json_object(){
    cJSON* root = cJSON_CreateObject();
    return root;
}

void* adapter_put_json_string_value(void* obj, const char* key, const char* value){
    if (!obj || !key || !value) return NULL;
    cJSON_AddStringToObject((cJSON*)obj, key, value);
    return obj;
}

char* adapter_serialize_json_to_string(void* obj){
    if (!obj) return NULL;
    char* json_string = cJSON_PrintUnformatted((cJSON*)obj);
    return json_string;
}


void* adapter_parse_json_string(const char* json_str){
    if (!json_str) return NULL;
    cJSON* root = cJSON_Parse(json_str);
    return root;
}

char* adapter_get_json_string_value(void* obj, const char* key){
    if (!obj || !key) return NULL;
    cJSON* item = cJSON_GetObjectItem((const cJSON*)obj, key);
    if (!item) return NULL;
    if (cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

void adapter_delete_json_object(void* obj){
    if (obj) {
        cJSON_Delete((cJSON*)obj);
    }
}

static StaticSemaphore_t s_sip_session_mutex_buf;
static SemaphoreHandle_t s_sip_session_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_sip_session_mutex_buf);

void adapter_lock_sip_mutex(){
    if (s_sip_session_mutex) {
        xSemaphoreTakeRecursive(s_sip_session_mutex, portMAX_DELAY);
        //LOG_INFO("@@@@@@@@@adapter_lock_sip_mutex got mutex");
    }
}

void adapter_unlock_sip_mutex(){
    if (s_sip_session_mutex) {
        xSemaphoreGiveRecursive(s_sip_session_mutex);
        //LOG_INFO("@@@@@@@@@adapter_unlock_sip_mutex released mutex");
    }
}


static SemaphoreHandle_t m_sip_list_mutex = NULL;
void adapter_lock_sip_list_mutex(){
    if (m_sip_list_mutex == NULL) {
        m_sip_list_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(m_sip_list_mutex, portMAX_DELAY);
}

void adapter_unlock_sip_list_mutex(){
    if (m_sip_list_mutex != NULL) {
        xSemaphoreGive(m_sip_list_mutex);
    }
}


int adapter_start_traffic_tunnel(media_parameter_ptr media_param){
    // To be implemented if needed
    return 0;
}

void adapter_clear_traffic_tunnel(){
    // To be implemented if needed
}


void adapter_transmit_mqtt_message(char* message){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->TransmitSIPMessage(std::string(message));
}

void on_call_established(char* session_id, media_parameter_ptr media_param){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->OnCallEstablished(std::string(session_id), media_param);
}


void on_call_ack_error(session_call_error_t error_code){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->OnCallAckError(error_code);
}

void on_call_terminated_by_server(){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->OnCallTerminatedByServer();
}

void on_call_terminated_ack(){
    // To be implemented if needed
}

void on_server_message_notify(server_message_notify_ptr notify){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->OnServerMessageNotify(notify);
}

void on_server_session_update_notify(message_session_event_ptr session_event){
    auto& app = Application::GetInstance();
    ((SipMqttProtocol*) app.GetProtocol())->OnServerSessionUpdateNotify(session_event);
}

void on_server_mcp_call(const char* message){
    /*
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    */


    auto& app = Application::GetInstance();
    app.ProcMcpMessage(std::string(message));
}