#ifndef CHARGE_H
#define CHARGE_H

#include "i2c_device.h"
#include <driver/i2c_master.h>
#include <driver/temperature_sensor.h>

class Charge : public I2cDevice {
public:
    Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr);
    ~Charge();

    void Printcharge();
    static void TaskFunction(void *pvParameters);

private:
    uint8_t* read_buffer_ = nullptr;
};

#endif // CHARGE_H
