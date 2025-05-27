#pragma once
#include <stdint.h>
#include <functional>
#include "bmi270.h"
#include "bmi270_legacy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class Bmi270Manager {
public:
    enum Feature {
        ACCEL_GYRO = 0x01,
        ANY_MOTION = 0x02,
        WRIST_GESTURE = 0x04,
        HIGH_G = 0x08,      // 新增高g
        LOW_G = 0x10        // 新增低g
    };

    // 手势输出字符串数组
    static const char* const GESTURE_OUTPUT_STRINGS[6];

    struct Config {
        uint8_t features; // Feature bitmask
        gpio_num_t int_pin; // GPIO pin for interrupt
    };

    // 定义回调函数类型
    using AnyMotionCallback = std::function<void()>;
    using WristGestureCallback = std::function<void(int)>;
    using AccelGyroCallback = std::function<void(float, float, float, float, float, float)>;
    using HighGCallback = std::function<void(uint8_t)>;
    using LowGCallback = std::function<void()>;

    Bmi270Manager();
    virtual ~Bmi270Manager();

    // 初始化BMI270，传入功能配置
    bool Init(const Config& config);

    // 设置回调函数
    void SetAnyMotionCallback(AnyMotionCallback cb) { any_motion_callback_ = cb; }
    void SetWristGestureCallback(WristGestureCallback cb) { wrist_gesture_callback_ = cb; }
    void SetAccelGyroCallback(AccelGyroCallback cb) { accel_gyro_callback_ = cb; }
    // 新增设置回调方法
    void SetHighGCallback(HighGCallback cb) { high_g_callback_ = cb; }
    void SetLowGCallback(LowGCallback cb) { low_g_callback_ = cb; }

    // 事件回调接口（可被主板类重载）
    virtual void OnAnyMotion();
    virtual void OnWristGesture(int gesture_id);
    virtual void OnAccelGyroData(float acc_x, float acc_y, float acc_z, float gyr_x, float gyr_y, float gyr_z);
    virtual void OnHighG(uint8_t high_g_out);
    virtual void OnLowG();

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

    // 回调函数
    AnyMotionCallback any_motion_callback_;
    WristGestureCallback wrist_gesture_callback_;
    AccelGyroCallback accel_gyro_callback_;
    HighGCallback high_g_callback_ = nullptr;
    LowGCallback low_g_callback_ = nullptr;

    // 任务实现
    static void AnyMotionTaskImpl(void* arg);
    static void AccelGyroTaskImpl(void* arg);
    static void GestureTaskImpl(void* arg);
    static void HighGTaskImpl(void* arg);
    static void LowGTaskImpl(void* arg);
    static void IRAM_ATTR GpioIsrHandler(void* arg);

    // 配置各功能
    int8_t ConfigureAccelGyro();
    int8_t ConfigureAnyMotion();
    int8_t ConfigureWristGesture();
    int8_t ConfigureHighG();
    int8_t ConfigureLowG();
    int8_t EnableSensors(const uint8_t* sensor_list, uint8_t num);
};