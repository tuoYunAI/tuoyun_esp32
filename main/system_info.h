#ifndef _SYSTEM_INFO_H_
#define _SYSTEM_INFO_H_

#include <string>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class SystemInfo {
public:
    static size_t GetFlashSize();
    static size_t GetMinimumFreeHeapSize();
    static size_t GetFreeHeapSize();
    static std::string GetMacAddress();
    static std::string GetChipModelName();
    static std::string GetUserAgent();
    static esp_err_t PrintTaskCpuUsage(TickType_t xTicksToWait);
    static void PrintTaskList();
    static void PrintHeapStats();
};

/**
 * @brief Create a task with stack allocated in PSRAM
 * 
 * @param pxTaskCode Pointer to the task entry function
 * @param pcName A descriptive name for the task
 * @param usStackDepth The number of bytes (not words!) in the task stack
 * @param pvParameters Pointer that will be used as the parameter for the task being created
 * @param uxPriority The priority at which the task should run
 * @param pxCreatedTask Used to pass back a handle by which the created task can be referenced
 * @return BaseType_t pdPASS if the task was successfully created and added to a ready list, otherwise an error code.
 */
BaseType_t xTaskCreateOnPsram(TaskFunction_t pxTaskCode,
                            const char * const pcName,
                            const configSTACK_DEPTH_TYPE usStackDepth,
                            void * const pvParameters,
                            UBaseType_t uxPriority,
                            TaskHandle_t * const pxCreatedTask);

/**
 * @brief Create a task with stack allocated in PSRAM and pinned to a specific core
 * 
 * @param pxTaskCode Pointer to the task entry function
 * @param pcName A descriptive name for the task
 * @param usStackDepth The number of bytes (not words!) in the task stack
 * @param pvParameters Pointer that will be used as the parameter for the task being created
 * @param uxPriority The priority at which the task should run
 * @param pxCreatedTask Used to pass back a handle by which the created task can be referenced
 * @param xCoreID The core to pin the task to (0 or 1, or tskNO_AFFINITY)
 * @return BaseType_t pdPASS if the task was successfully created and added to a ready list, otherwise an error code.
 */
BaseType_t xTaskCreateOnPsramPinnedToCore(TaskFunction_t pxTaskCode,
                                        const char * const pcName,
                                        const configSTACK_DEPTH_TYPE usStackDepth,
                                        void * const pvParameters,
                                        UBaseType_t uxPriority,
                                        TaskHandle_t * const pxCreatedTask,
                                        const BaseType_t xCoreID);

#endif // _SYSTEM_INFO_H_
