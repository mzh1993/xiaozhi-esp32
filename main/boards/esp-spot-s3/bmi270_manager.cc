#include "bmi270_manager.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <string.h>

#define TAG "Bmi270Manager"

Bmi270Manager::Bmi270Manager() {}
Bmi270Manager::~Bmi270Manager() {}

bool Bmi270Manager::Init(const Config& config) {
    // 保存中断引脚
    int_pin_ = config.int_pin;
    if (int_pin_ == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid interrupt pin configuration");
        return false;
    }

    // 1. 创建信号量
    if (any_motion_semaphore_ == nullptr) {
        any_motion_semaphore_ = xSemaphoreCreateBinary();
        if (any_motion_semaphore_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create any_motion_semaphore_");
            return false;
        }
    }

    // 2. 配置GPIO中断
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL << int_pin_);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    
    esp_err_t gpio_ret = gpio_config(&io_conf);
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(gpio_ret));
        vSemaphoreDelete(any_motion_semaphore_);
        any_motion_semaphore_ = nullptr;
        return false;
    }

    // 3. 安装GPIO ISR服务
    if (!any_motion_isr_service_installed_) {
        esp_err_t gpio_isr_ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
        if (gpio_isr_ret == ESP_OK) {
            any_motion_isr_service_installed_ = true;
        } else if (gpio_isr_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "GPIO ISR service already installed");
            any_motion_isr_service_installed_ = true;
        } else {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(gpio_isr_ret));
            vSemaphoreDelete(any_motion_semaphore_);
            any_motion_semaphore_ = nullptr;
            return false;
        }
    }

    // 4. 添加GPIO ISR处理程序
    gpio_isr_handler_remove(int_pin_);
    esp_err_t add_isr_ret = gpio_isr_handler_add(int_pin_, GpioIsrHandler, this);
    if (add_isr_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(add_isr_ret));
        vSemaphoreDelete(any_motion_semaphore_);
        any_motion_semaphore_ = nullptr;
        return false;
    }

    // 5. 根据config.features启用功能
    if (config.features & ACCEL_GYRO) {
        if (ConfigureAccelGyro() != BMI2_OK) return false;
        // 启动数据读取任务
        xTaskCreate(AccelGyroTaskImpl, "bmi270_accel_gyro", 4096, this, 5, &accel_gyro_task_handle_);
        ESP_LOGI(TAG, "AccelGyro feature enabled successfully");
    }
    if (config.features & ANY_MOTION) {
        if (ConfigureAnyMotion() != BMI2_OK) return false;
        // 启动任意运动检测任务
        xTaskCreate(AnyMotionTaskImpl, "bmi270_any_motion", 4096, this, 5, &any_motion_task_handle_);
        ESP_LOGI(TAG, "Any Motion feature enabled successfully");
    }
    if (config.features & WRIST_GESTURE) {
        if (ConfigureWristGesture() != BMI2_OK) return false;
        // 启动手势识别任务
        xTaskCreate(GestureTaskImpl, "bmi270_gesture", 4096, this, 5, &gesture_task_handle_);
        ESP_LOGI(TAG, "Wrist Gesture feature enabled successfully");
    }
    return true;
}

void Bmi270Manager::OnAnyMotion() {
    if (any_motion_callback_) {
        any_motion_callback_();
        return;
    }
    ESP_LOGI(TAG, "Any Motion detected (default handler)");
}

void Bmi270Manager::OnWristGesture(int gesture_id) {
    if (wrist_gesture_callback_) {
        wrist_gesture_callback_(gesture_id);
        return;
    }
    ESP_LOGI(TAG, "Wrist Gesture detected: %d (default handler)", gesture_id);
}

void Bmi270Manager::OnAccelGyroData(float acc_x, float acc_y, float acc_z, float gyr_x, float gyr_y, float gyr_z) {
    if (accel_gyro_callback_) {
        accel_gyro_callback_(acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z);
        return;
    }
    ESP_LOGI(TAG, "Accel: %.2f %.2f %.2f, Gyro: %.2f %.2f %.2f (default handler)", acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z);
}

struct bmi2_dev* Bmi270Manager::GetBmi2Dev() { return bmi_dev_; }

// 任务实现
void Bmi270Manager::AnyMotionTaskImpl(void* arg) {
    auto* self = static_cast<Bmi270Manager*>(arg);
    uint16_t int_status = 0;
    while (true) {
        if (xSemaphoreTake(self->any_motion_semaphore_, portMAX_DELAY) == pdTRUE) {
            if (bmi2_get_int_status(&int_status, self->bmi_dev_) == BMI2_OK) {
                if (int_status & BMI270_ANY_MOT_STATUS_MASK) {
                    self->OnAnyMotion();
                }
            }
        }
    }
}

void Bmi270Manager::AccelGyroTaskImpl(void* arg) {
    auto* self = static_cast<Bmi270Manager*>(arg);
    struct bmi2_sens_data sensor_data;
    const float ACCEL_G_RANGE = 2.0f;
    const float GYRO_DPS_RANGE = 2000.0f;
    while (true) {
        if (bmi2_get_sensor_data(&sensor_data, self->bmi_dev_) == BMI2_OK) {
            if ((sensor_data.status & BMI2_DRDY_ACC) && (sensor_data.status & BMI2_DRDY_GYR)) {
                float acc_x = sensor_data.acc.x * ACCEL_G_RANGE / 32768.0f * 9.80665f;
                float acc_y = sensor_data.acc.y * ACCEL_G_RANGE / 32768.0f * 9.80665f;
                float acc_z = sensor_data.acc.z * ACCEL_G_RANGE / 32768.0f * 9.80665f;
                float gyr_x = sensor_data.gyr.x * GYRO_DPS_RANGE / 32768.0f;
                float gyr_y = sensor_data.gyr.y * GYRO_DPS_RANGE / 32768.0f;
                float gyr_z = sensor_data.gyr.z * GYRO_DPS_RANGE / 32768.0f;
                self->OnAccelGyroData(acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Bmi270Manager::GestureTaskImpl(void* arg) {
    auto* self = static_cast<Bmi270Manager*>(arg);
    uint16_t int_status = 0;
    struct bmi2_feat_sensor_data sens_data = { .type = BMI2_WRIST_GESTURE };
    while (true) {
        bmi2_get_int_status(&int_status, self->bmi_dev_);
        if (int_status & BMI270_WRIST_GEST_STATUS_MASK) {
            if (bmi270_get_feature_data(&sens_data, 1, self->bmi_dev_) == BMI2_OK) {
                int id = sens_data.sens_data.wrist_gesture_output;
                self->OnWristGesture(id);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void IRAM_ATTR Bmi270Manager::GpioIsrHandler(void* arg) {
    auto* self = static_cast<Bmi270Manager*>(arg);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (self->any_motion_semaphore_ != nullptr) {
        xSemaphoreGiveFromISR(self->any_motion_semaphore_, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// 配置各功能（略，参考原有实现，需根据实际板子完善）
int8_t Bmi270Manager::ConfigureAccelGyro() { return BMI2_OK; }

int8_t Bmi270Manager::ConfigureAnyMotion() {
    if (!bmi_dev_) {
        ESP_LOGE(TAG, "BMI2 device handle is NULL!");
        return BMI2_E_NULL_PTR;
    }

    // 1. 配置Any Motion参数
    struct bmi2_sens_config config;
    config.type = BMI2_ANY_MOTION;
    int8_t rslt = bmi270_get_sensor_config(&config, 1, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get Any Motion config: %d", rslt);
        return rslt;
    }

    // 设置Any Motion参数
    config.cfg.any_motion.duration = 0x04;  // 80ms
    config.cfg.any_motion.threshold = 0x68; // 50mg

    rslt = bmi270_set_sensor_config(&config, 1, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to set Any Motion config: %d", rslt);
        return rslt;
    }

    // 2. 配置中断引脚
    struct bmi2_int_pin_config pin_config = { 0 };
    rslt = bmi2_get_int_pin_config(&pin_config, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get interrupt pin config: %d", rslt);
        return rslt;
    }

    pin_config.pin_type = BMI2_INT1;
    pin_config.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    pin_config.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW;
    pin_config.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    pin_config.int_latch = BMI2_INT_NON_LATCH;

    rslt = bmi2_set_int_pin_config(&pin_config, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to set interrupt pin config: %d", rslt);
        return rslt;
    }

    // 3. 配置Any Motion中断
    struct bmi2_sens_int_config sens_int_cfg = { 
        .type = BMI2_ANY_MOTION, 
        .hw_int_pin = BMI2_INT1 
    };

    // 4. 启用加速度计和Any Motion功能
    uint8_t sensor_list[2] = { BMI2_ACCEL, BMI2_ANY_MOTION };
    rslt = bmi270_sensor_enable(sensor_list, 2, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable accelerometer and Any Motion: %d", rslt);
        return rslt;
    }

    // 5. 映射Any Motion中断到INT1
    rslt = bmi270_map_feat_int(&sens_int_cfg, 1, bmi_dev_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to map Any Motion interrupt: %d", rslt);
        return rslt;
    }

    ESP_LOGI(TAG, "Any Motion feature configured successfully");
    return BMI2_OK;
}

int8_t Bmi270Manager::ConfigureWristGesture() { return BMI2_OK; }

int8_t Bmi270Manager::EnableSensors(const uint8_t* sensor_list, uint8_t num) { return BMI2_OK; } 