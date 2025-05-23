#pragma once
#include <stdint.h>
#include <functional>
#include "bmi270.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class Bmi270Manager {
public:
    enum Feature {
        ACCEL_GYRO = 0x01,
        ANY_MOTION = 0x02,
        WRIST_GESTURE = 0x04
    };

    struct Config {
        uint8_t features; // Feature bitmask
        gpio_num_t int_pin; // GPIO pin for interrupt
    };

    Bmi270Manager();
    virtual ~Bmi270Manager();

    // 初始化BMI270，传入功能配置
    bool Init(const Config& config);

    // 事件回调接口（可被主板类重载）
    virtual void OnAnyMotion();
    virtual void OnWristGesture(int gesture_id);
    virtual void OnAccelGyroData(float acc_x, float acc_y, float acc_z, float gyr_x, float gyr_y, float gyr_z);

    // 允许主板类获取底层bmi2_dev指针
    struct bmi2_dev* GetBmi2Dev();

    // 公开BMI270设备句柄，允许外部设置
    struct bmi2_dev* bmi_dev_ = nullptr;

protected:
    // 供子类访问
    SemaphoreHandle_t any_motion_semaphore_ = nullptr;
    TaskHandle_t any_motion_task_handle_ = nullptr;
    TaskHandle_t accel_gyro_task_handle_ = nullptr;
    TaskHandle_t gesture_task_handle_ = nullptr;
    bool any_motion_isr_service_installed_ = false;
    gpio_num_t int_pin_ = GPIO_NUM_NC; // GPIO pin for interrupt

    // 任务实现
    static void AnyMotionTaskImpl(void* arg);
    static void AccelGyroTaskImpl(void* arg);
    static void GestureTaskImpl(void* arg);
    static void IRAM_ATTR GpioIsrHandler(void* arg);

    // 配置各功能
    int8_t ConfigureAccelGyro();
    int8_t ConfigureAnyMotion();
    int8_t ConfigureWristGesture();
    int8_t EnableSensors(const uint8_t* sensor_list, uint8_t num);
}; 