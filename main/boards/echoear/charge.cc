#include "charge.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/temperature_sensor.h>

extern temperature_sensor_handle_t temp_sensor;

Charge::Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr)
{
    read_buffer_ = new uint8_t[8];
}

Charge::~Charge()
{
    delete[] read_buffer_;
}

void Charge::Printcharge()
{
    ReadRegs(0x08, read_buffer_, 2);
    ReadRegs(0x0c, read_buffer_ + 2, 2);

    // Read temperature if sensor is available
    if (temp_sensor != NULL) {
        float temperature = 0.0f;
        esp_err_t ret = temperature_sensor_get_celsius(temp_sensor, &temperature);
        if (ret == ESP_OK) {
            // Temperature read successfully, can be used if needed
            (void)temperature;
        }
    }

    int16_t voltage = static_cast<uint16_t>(read_buffer_[1] << 8 | read_buffer_[0]);
    int16_t current = static_cast<int16_t>(read_buffer_[3] << 8 | read_buffer_[2]);

    // Use the variables to avoid warnings (can be removed if actual implementation uses them)
    (void)voltage;
    (void)current;
}

void Charge::TaskFunction(void *pvParameters)
{
    Charge* charge = static_cast<Charge*>(pvParameters);
    while (true) {
        charge->Printcharge();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
