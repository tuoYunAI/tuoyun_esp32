#ifndef IMU_GESTURE_H
#define IMU_GESTURE_H

#include "driver/i2c.h" // 先加载官方定义防止冲突

#ifdef __cplusplus
extern "C"
{
#endif

// 如果 i2c_bus.h 还是报错，可以启用下面的宏拦截
// #define i2c_config_t i2c_bus_config_t_temp
#include "bmi270.h"
    // #undef i2c_config_t

    /**
     * @brief 初始化 BMI270 并开启 3 次摇晃检测
     * @param int1_pin 硬件连接的中断引脚编号 (如 GPIO_NUM_4)
     */
    void imu_gesture_init(int int1_pin);

#ifdef __cplusplus
}
#endif

#endif