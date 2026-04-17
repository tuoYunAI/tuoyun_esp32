#include "imu_gesture.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "IMU_GEST";
static struct bmi2_dev bmi2_device;
static QueueHandle_t gpio_evt_queue = NULL;

// --- I2C 适配层 (需要根据你的项目实际 I2C 接口修改) ---
static int8_t bus_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    // 示例：使用 ESP-IDF 原生驱动
    // return i2c_master_write_read_device(...);
    return BMI2_OK;
}

static int8_t bus_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    return BMI2_OK;
}

static void delay_us(uint32_t period, void *intf_ptr)
{
    vTaskDelay(pdMS_TO_TICKS(period / 1000 + 1));
}

// --- 中断任务处理 ---
static void imu_intr_task(void *arg)
{
    uint32_t io_num;
    int shake_counter = 0;
    int64_t last_shake_time = 0;
    int64_t window_start_time = 0;

    const int TARGET_COUNT = 3;  // 摇晃 3 次
    const int WINDOW_MS = 2000;  // 2秒窗口
    const int INTERVAL_MS = 250; // 去抖间隔

    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            uint16_t int_status = 0;
            bmi2_get_int_status(&int_status, &bmi2_device);

            if (int_status & BMI2_ANY_MOT_INT_MASK)
            {
                int64_t now = esp_timer_get_time() / 1000;

                if ((now - last_shake_time) < INTERVAL_MS)
                    continue;

                if ((now - window_start_time) > WINDOW_MS)
                {
                    shake_counter = 1;
                    window_start_time = now;
                }
                else
                {
                    shake_counter++;
                    ESP_LOGI(TAG, "摇晃计数: %d", shake_counter);
                }

                last_shake_time = now;

                if (shake_counter >= TARGET_COUNT)
                {
                    ESP_LOGW(TAG, "★★★ 检测到连续摇晃 3 次！ ★★★");
                    shake_counter = 0;
                }
            }
        }
    }
}

// --- 初始化与配置 ---
void imu_gesture_init(int int1_pin)
{
    // 1. 硬件句柄设置
    bmi2_device.read = bus_read;
    bmi2_device.write = bus_write;
    bmi2_device.delay_us = delay_us;
    bmi2_device.intf = BMI2_I2C_INTF;

    // 2. 初始化芯片并加载配置文件
    if (bmi270_init(&bmi2_device) != BMI2_OK)
    {
        ESP_LOGE(TAG, "BMI270 启动失败");
        return;
    }

    // 3. 配置 Any-Motion
    struct bmi2_any_motion_config config = {
        .duration = 2,    // 触发非常快
        .threshold = 200, // 灵敏度 (1 LSB = 0.48mg)
        .select_x = 1,
        .select_y = 1,
        .select_z = 1};
    bmi270_set_any_motion_config(&config, &bmi2_device);

    // 启用传感器功能并映射中断
    uint8_t sensor_list = BMI2_ANY_MOTION;
    bmi270_sensor_enable(&sensor_list, 1, &bmi2_device);
    bmi270_map_feat_int(BMI2_ANY_MOTION, BMI2_INT1, &bmi2_device);

    // 4. 配置 ESP32 GPIO 中断
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.pin_bit_mask = (1ULL << int1_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(imu_intr_task, "imu_task", 4096, NULL, 10, NULL);

    gpio_install_isr_service(0);
    gpio_add_isr_handler((gpio_num_t)int1_pin, [](void *arg)
                         {
        uint32_t pin = (uint32_t)arg;
        xQueueSendFromISR(gpio_evt_queue, &pin, NULL); }, (void *)int1_pin);

    ESP_LOGI(TAG, "摇晃检测系统已就绪");
}