#include "wifi_board.h" // 包含 WiFi 板的头文件
#include "audio_codecs/es8311_audio_codec.h" // 包含 ES8311 音频编解码器的头文件
#include "application.h" // 包含应用程序的头文件
#include "button.h" // 包含按钮的头文件
#include "config.h" // 包含配置的头文件
#include "iot/thing_manager.h" // 包含 IoT 设备管理器的头文件
#include "sdkconfig.h" // 包含 SDK 配置的头文件
#include <string.h>

#include "driver/gpio.h"
#include "freertos/task.h"
#include <wifi_station.h> // 包含 WiFi 站点的头文件
#include <esp_log.h> // 包含 ESP 日志的头文件
#include <driver/i2c_master.h> // 包含 I2C 主机驱动的头文件
#include <driver/spi_common.h> // 包含 SPI 公共驱动的头文件
#include "esp_adc/adc_oneshot.h" // 包含 ESP ADC 单次采样驱动的头文件
#include "esp_adc/adc_cali.h" // 包含 ESP ADC 校准的头文件
#include "esp_adc/adc_cali_scheme.h" // 包含 ESP ADC 校准方案的头文件

#include <driver/gpio.h> // 包含 GPIO 驱动的头文件
#include "esp_timer.h" // 包含 ESP 定时器的头文件
#include "led/circular_strip.h" // 包含环形 LED 条的头文件

// #include "bmi270Imu.h" 
#include "bmi270.h"       
#include "common/common.h"

///
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bmi2.h"
#include "bmi270.h" // Assuming this contains BMI270_ANY_MOT_STATUS_MASK and other helpers

// Helper function (from original) - ensure this is defined elsewhere
extern void bmi2_error_codes_print_result(int8_t rslt);
// Placeholder for the actual feature configuration (from original)
// This function is critical and needs to set any-motion parameters (threshold, duration)
extern int8_t set_feature_config(struct bmi2_dev *dev);
static const char *TAG = "ANY_MOTION"; // Logging Tag

// --- Globals for ISR and Task communication ---
static SemaphoreHandle_t s_any_motion_semaphore = NULL;

// --- Forward declaration for the task ---
static void any_motion_event_handler_task(void *pvParameters);

// --- ISR Handler ---
static void IRAM_ATTR gpio_isr_edge_handler_anymotion(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_any_motion_semaphore != NULL) {
        xSemaphoreGiveFromISR(s_any_motion_semaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}
///

#define TAG "esp_spot_s3" // 定义日志标签

bool button_released_ = false; // 按钮释放标志
bool shutdown_ready_ = false; // 关机准备标志
esp_timer_handle_t shutdown_timer; // 关机定时器句柄

class EspSpotS3Bot : public WifiBoard { // 定义 EspSpotS3Bot 类，继承自 WifiBoard
private:
    i2c_master_bus_handle_t i2c_bus_; // I2C 总线句柄
    // —— 新增：底层 I2C，用于 bmi270 ——  
    i2c_bus_handle_t lowlevel_i2c_bus_ = nullptr;
    // BMI270 句柄
    bmi270_handle_t bmi_handle_ = nullptr;
    Button boot_button_; // 启动按钮对象
    Button key_button_; // 键按钮对象
    adc_oneshot_unit_handle_t adc1_handle; // ADC 单次采样单元句柄
    adc_cali_handle_t adc1_cali_handle; // ADC 校准句柄
    bool do_calibration = false; // 是否进行校准标志
    bool key_long_pressed = false; // 键长按标志
    int64_t last_key_press_time = 0; // 上次按键时间
    static const int64_t LONG_PRESS_TIMEOUT_US = 5 * 1000000ULL; // 长按超时时间

    void InitializeI2c() {
        ESP_LOGI(TAG, "InitializeI2c: Using SDA_PIN=%d, SCL_PIN=%d",
                I2C_MASTER_SDA_PIN, I2C_MASTER_SCL_PIN);

        // —— 高层 I2C 总线配置 ——  
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_MASTER_SDA_PIN,
            .scl_io_num = I2C_MASTER_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // —— 底层 bus：给 BMI270 用 ——  
        {
            i2c_config_t bus_conf;
            memset(&bus_conf, 0, sizeof(bus_conf));
            bus_conf.mode           = I2C_MODE_MASTER;
            bus_conf.sda_io_num     = I2C_MASTER_SDA_PIN;
            bus_conf.sda_pullup_en  = GPIO_PULLUP_ENABLE;
            bus_conf.scl_io_num     = I2C_MASTER_SCL_PIN;
            bus_conf.scl_pullup_en  = GPIO_PULLUP_ENABLE;
            bus_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;  // 100kHz
            this->lowlevel_i2c_bus_ = i2c_bus_create(I2C_NUM_0, &bus_conf);
            if (!this->lowlevel_i2c_bus_) {
                ESP_LOGE(TAG, "Low-level I2C bus (for BMI270) creation failed!");
                abort(); 
            } else {
                ESP_LOGI(TAG, "Low-level I2C bus (lowlevel_i2c_bus_ for BMI270) created: %p", this->lowlevel_i2c_bus_);
            }
        }
        // Logging for clarity (from your original code)
        ESP_LOGI(TAG, "bmi270_enable_wrist_gesture: Entry. i2c_bus_ (high-level) handle: %p", this->i2c_bus_);
        ESP_LOGI(TAG, "GetAudioCodec: Entry. i2c_bus_ (high-level) handle before static audio_codec init: %p", this->i2c_bus_);
        
        // 2) 创建 BMI270 传感器对象
        bmi270_i2c_config_t bmi_conf = {
            .i2c_handle = lowlevel_i2c_bus_,
            .i2c_addr   = BMI270_I2C_ADDRESS,  // 0x68 或 0x69
        };
        esp_err_t err = bmi270_sensor_create(&bmi_conf, &bmi_handle_);
        if (err != ESP_OK || !bmi_handle_) {
            ESP_LOGE(TAG, "bmi270_enable_wrist_gesture: bmi270_sensor_create failed (%d)", err);
            return;
        }
        ESP_LOGI(TAG, "BMI270 sensor handle: %p", bmi_handle_);
    }

    int8_t set_accel_gyro_config(struct bmi2_dev *dev) {
        // Example configuration (ADAPT THIS TO YOUR NEEDS)
        int8_t rslt;
        struct bmi2_sens_config config[2];

        // Configure Accelerometer
        config[0].type = BMI2_ACCEL;
        rslt = bmi2_get_sensor_config(&config[0], 1, dev);
        if (rslt != BMI2_OK) return rslt;
        config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ; // Output Data Rate
        config[0].cfg.acc.range = BMI2_ACC_RANGE_2G; // Range (+/- 2G, 4G, 8G, 16G)
        config[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4; // Bandwidth parameter (filter)
        config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE; // Filter performance mode

        // Configure Gyroscope
        config[1].type = BMI2_GYRO;
        rslt = bmi2_get_sensor_config(&config[1], 1, dev);
        if (rslt != BMI2_OK) return rslt;
        config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ; // Output Data Rate
        config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000; // Range (+/- 125, 250, 500, 1000, 2000 DPS)
        config[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE; // Bandwidth parameter
        config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE; // Filter performance mode
        config[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE; // Noise performance mode

        rslt = bmi2_set_sensor_config(config, 2, dev);
        if (rslt != BMI2_OK) return rslt;

        ESP_LOGI(TAG, "Accelerometer configured: ODR=100Hz, Range=2G"); // Adjust log based on actual settings
        ESP_LOGI(TAG, "Gyroscope configured: ODR=100Hz, Range=2000DPS"); // Adjust log based on actual settings
        
        return BMI2_OK;
    }

    float lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width) {
        // Formula: (val / 2^(bit_width-1)) * g_range * 9.80665
        // For BMI270, bit_width is typically 16
        double power_of_2 = 1 << (bit_width - 1);
        return (val / power_of_2) * g_range * 9.80665f;
    }

    float lsb_to_dps(int16_t val, float dps_range, uint8_t bit_width) {
        // Formula: (val / 2^(bit_width-1)) * dps_range
        // For BMI270, bit_width is typically 16
        double power_of_2 = 1 << (bit_width - 1);
        return (val / power_of_2) * dps_range;
    }

    void bmi270_enable_accel_gyro() {
        ESP_LOGI(TAG, "bmi270_enable_accel_gyro (low-level espressif2022/bmi270)…");

        // 1) Check if I2C bus and BMI handle are initialized
        if (!lowlevel_i2c_bus_ || !bmi_handle_) { // Assuming lowlevel_i2c_bus_ is your flag/object for I2C init
            ESP_LOGE(TAG, "bmi270_enable_accel_gyro failed: lowlevel_i2c_bus_ or bmi_handle_ is not initialized!");
            return;
        }

        int8_t rslt;

        // 2) Configure Accelerometer and Gyroscope settings
        // It's important that set_accel_gyro_config correctly sets ODR, Range, etc.
        rslt = set_accel_gyro_config(this->bmi_handle_);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "set_accel_gyro_config failed. BMI2 API Error: %d", rslt);
            // You might want to use bmi2_error_codes_print_result(rslt, bmi_handle_); or similar if available and logs to ESP_LOG
            return;
        }
        ESP_LOGI(TAG, "Accelerometer and Gyroscope configured.");

        // 3) Enable Accelerometer and Gyroscope sensors
        uint8_t sensor_list[] = { BMI2_ACCEL, BMI2_GYRO };
        // uint8_t num_sensors = sizeof(sensor_list) / sizeof(sensor_list[0]); // This is 2
        rslt = bmi2_sensor_enable(sensor_list, 2, this->bmi_handle_);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "bmi2_sensor_enable for Accel/Gyro failed. BMI2 API Error: %d", rslt);
            return;
        }
        ESP_LOGI(TAG, "Accelerometer and Gyroscope sensors enabled.");

        // 4) Create a task to continuously read Accelerometer and Gyroscope data
        BaseType_t task_created = xTaskCreate(
            [](void* arg) { static_cast<EspSpotS3Bot*>(arg)->AccelGyroReadTask(); },
            "accel_gyro_task", // Task name
            4096,              // Stack size (adjust as needed)
            this,              // Parameter passed to the task
            5,                 // Priority
            NULL               // Task handle (optional)
        );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create accel_gyro_task. Error code: %d", task_created);
        } else {
            ESP_LOGI(TAG, "accel_gyro_task created successfully.");
        }
    }

    void AccelGyroReadTask() {
        ESP_LOGI(TAG, "AccelGyroReadTask started.");

        struct bmi2_sens_data sensor_data; // Structure to hold sensor data (accel, gyro, etc.)
        int8_t rslt;

        // These ranges MUST match the configuration set in set_accel_gyro_config
        // It's better to fetch these from the config or store them after setting
        const float ACCEL_G_RANGE = 2.0f;    // e.g., 2G. Corresponds to BMI2_ACC_RANGE_2G
        const float GYRO_DPS_RANGE = 2000.0f; // e.g., 2000DPS. Corresponds to BMI2_GYR_RANGE_2000DPS

        while (true) {
            // 1) Get sensor data for accelerometer and gyroscope
            // This function gets data for all enabled sensors if they are time-synchronized.
            // If not, you might need to get them separately or check status flags.
            rslt = bmi2_get_sensor_data(&sensor_data, this->bmi_handle_);

            if (rslt == BMI2_OK) {
                // 2) Check if new data is available using Data Ready status bits
                // It's good practice to check these flags.
                // sensor_data.status contains flags like BMI2_DRDY_ACC, BMI2_DRDY_GYR
                if (sensor_data.status & BMI2_DRDY_ACC) {
                    // Convert raw accelerometer data to m/s^2
                    float acc_x = lsb_to_mps2(sensor_data.acc.x, ACCEL_G_RANGE, this->bmi_handle_->resolution);
                    float acc_y = lsb_to_mps2(sensor_data.acc.y, ACCEL_G_RANGE, this->bmi_handle_->resolution);
                    float acc_z = lsb_to_mps2(sensor_data.acc.z, ACCEL_G_RANGE, this->bmi_handle_->resolution);

                    // ESP_LOGD is good for frequent data, ESP_LOGI for less frequent/important updates
                    ESP_LOGD(TAG, "ACC Raw: X=%3d Y=%3d Z=%3d", sensor_data.acc.x, sensor_data.acc.y, sensor_data.acc.z);
                    ESP_LOGI(TAG, "ACC (m/s^2): X=%3.2f Y=%3.2f Z=%3.2f", acc_x, acc_y, acc_z);
                } else {
                    // ESP_LOGD(TAG, "Accelerometer data not ready (status: 0x%02X)", sensor_data.status);
                }

                if (sensor_data.status & BMI2_DRDY_GYR) {
                    // Convert raw gyroscope data to degrees/second
                    float gyr_x = lsb_to_dps(sensor_data.gyr.x, GYRO_DPS_RANGE, this->bmi_handle_->resolution);
                    float gyr_y = lsb_to_dps(sensor_data.gyr.y, GYRO_DPS_RANGE, this->bmi_handle_->resolution);
                    float gyr_z = lsb_to_dps(sensor_data.gyr.z, GYRO_DPS_RANGE, this->bmi_handle_->resolution);

                    ESP_LOGD(TAG, "GYR Raw:  X=%3d Y=%3d Z=%3d", sensor_data.gyr.x, sensor_data.gyr.y, sensor_data.gyr.z);
                    ESP_LOGI(TAG, "GYR (dps): X=%3.2f Y=%3.2f Z=%3.2f", gyr_x, gyr_y, gyr_z);
                } else {
                    // ESP_LOGD(TAG, "Gyroscope data not ready (status: 0x%02X)", sensor_data.status);
                }

                // If you only want to log when *both* are ready, combine the checks:
                // if ((sensor_data.status & BMI2_DRDY_ACC) && (sensor_data.status & BMI2_DRDY_GYR)) {
                //    ... process both ...
                // }

            } else {
                ESP_LOGE(TAG, "AccelGyroReadTask: bmi2_get_sensor_data failed. BMI2 API Error: %d", rslt);
            }

            // Delay before next read. Adjust based on ODR and application needs.
            // If ODR is 100Hz, new data is available every 10ms.
            // Polling slightly less frequently than ODR is fine, e.g., every 50-100ms.
            vTaskDelay(pdMS_TO_TICKS(100)); // e.g., read 10 times per second
        }
    }

    void bmi270_enable_wrist_gesture() {
        ESP_LOGI(TAG, "bmi270_enable_wrist_gesture (low-level espressif2022/bmi270)…");

        // 1) 创建并配置底层 I2C 总线（如果还没创建的话）
        if (!lowlevel_i2c_bus_ || !bmi_handle_) {
            ESP_LOGE(TAG, "bmi270_enable_wrist_gesture failed: lowlevel_i2c_bus_ or bmi_handle_ failed!", );
            return;
        }

        // 3) 使能相关检测
        uint8_t sens_list[] = { BMI2_ACCEL, BMI2_WRIST_GESTURE };
        uint8_t num_sensors = sizeof(sens_list) / sizeof(sens_list[0]); // 正确计算传感器数量
        bmi270_sensor_enable(sens_list, num_sensors, bmi_handle_);
        // 4) 配置手势——检测哪只手、灵敏度等
        struct bmi2_sens_config cfg = { .type = BMI2_WRIST_GESTURE };
        bmi270_get_sensor_config(&cfg, 1, bmi_handle_);
        cfg.cfg.wrist_gest.wearable_arm = BMI2_ARM_LEFT;
        bmi270_set_sensor_config(&cfg, 1, bmi_handle_);
        // 5) 将手势中断映射到 INT1 引脚
        struct bmi2_sens_int_config int_cfg = {
            .type       = BMI2_WRIST_GESTURE,
            .hw_int_pin = BMI2_INT1
        };

        esp_err_t err = bmi270_map_feat_int(&int_cfg, 1, this->bmi_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bmi270_map_feat_int failed: %s", esp_err_to_name(err));
            return;
        }

        ESP_LOGI(TAG, "BMI270 initialized and configured.");

        BaseType_t task_created = xTaskCreate(
            [](void* arg) { static_cast<EspSpotS3Bot*>(arg)->ImuEventHandlerTask(); },
            "gesture_task", 4096, this, 5, NULL );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create gesture_task. Error code: %d", task_created);
        } else {
            ESP_LOGI(TAG, "gesture_task created successfully.");
        }

    }

    int8_t bmi270_enable_any_motion(struct bmi2_dev *bmi2_dev)
    {
        int8_t rslt = BMI2_OK;
        ESP_LOGI(TAG, "Initializing Any Motion Detection...");

        // 1. Check BMI2 device handle (similar to reference code's check)
        if (!bmi2_dev) {
            ESP_LOGE(TAG, "BMI2 device handle is NULL!");
            return BMI2_E_NULL_PTR;
        }

        // 2. Create semaphore for ISR-task communication
        if (s_any_motion_semaphore == NULL) { // Create only once
            s_any_motion_semaphore = xSemaphoreCreateBinary();
            if (s_any_motion_semaphore == NULL) {
                ESP_LOGE(TAG, "Failed to create any_motion semaphore!");
                return BMI2_E_COM_FAIL; // Or a more appropriate error
            }
        } else {
            // If called again, ensure semaphore is empty before reuse
            xSemaphoreTake(s_any_motion_semaphore, 0);
        }


        // 3. Configure GPIO pin for interrupt from BMI270
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_ANYEDGE;       // 设置中断类型为任意边沿触发 (上升沿或下降沿)
                                                    // BMI270 的中断引脚电平变化时都会触发
        io_conf.pin_bit_mask = (1ULL << I2C_INT_IO); // 指定用于中断的 GPIO 引脚号 (I2C_INT_IO 是一个宏定义)
        io_conf.mode = GPIO_MODE_INPUT;              // 设置 GPIO模式为输入
        io_conf.pull_down_en = 1;                    // 使能内部下拉电阻。
                                                    // 这取决于 BMI270 中断引脚的电气特性 (例如，如果中断是高电平有效，
                                                    // 且空闲时可能悬空，则下拉可以确保其在空闲时为低电平)
        gpio_config(&io_conf);                       // 应用 GPIO 配置

        // Install GPIO ISR service (if not already installed globally)
        // ESP_INTR_FLAG_DEFAULT (0) is fine.
        // This might fail if already installed. Consider checking return or installing once globally.
        static bool isr_service_installed = false;
        if (!isr_service_installed) {
            esp_err_t gpio_isr_ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1); // Or other appropriate flags
            if (gpio_isr_ret == ESP_OK) {
                isr_service_installed = true;
            } else if (gpio_isr_ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "GPIO ISR service already installed.");
                isr_service_installed = true; // Assume it's usable
            } else {
                ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(gpio_isr_ret));
                return BMI2_E_COM_FAIL; // Or map esp_err_t to int8_t
            }
        }

        // Add ISR handler for the specific pin
        // Remove previous handler if one exists for this pin, then add.
        gpio_isr_handler_remove(I2C_INT_IO);
        esp_err_t add_isr_ret = gpio_isr_handler_add(I2C_INT_IO, gpio_isr_edge_handler_anymotion, (void*) I2C_INT_IO);
        if (add_isr_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(add_isr_ret));
            return BMI2_E_COM_FAIL;
        }
        gpio_intr_enable(I2C_INT_IO); // Ensure interrupt is enabled for the pin

        // 4. Enable required sensors: Accelerometer and Any Motion feature
        uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_ANY_MOTION };
        ESP_LOGI(TAG, "Enabling Accel and Any Motion sensors...");
        // Assuming bmi270_sensor_enable is a wrapper for bmi2_sensor_enable
        rslt = bmi2_sensor_enable(sens_list, 2, bmi2_dev);
        bmi2_error_codes_print_result(rslt);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "bmi2_sensor_enable failed.");
            return rslt;
        }

        // 5. Configure Any Motion feature (thresholds, duration, etc.)
        // This is where set_feature_config() from your original code is called.
        // It's crucial that this function properly configures the any-motion detection parameters.
        // For example, it might call bmi2_set_adv_power_save or bmi2_set_feature_config internally.
        //
        // If you wanted to configure it like the wrist gesture example:
        // struct bmi2_sens_config sens_cfg = {0};
        // sens_cfg.type = BMI2_ANY_MOTION;
        // rslt = bmi2_get_sensor_config(&sens_cfg, 1, bmi2_dev);
        // bmi2_error_codes_print_result(rslt);
        // if (rslt == BMI2_OK) {
        //     // Modify sens_cfg.cfg.any_motion parameters here
        //     // e.g., sens_cfg.cfg.any_motion.threshold = YOUR_THRESHOLD; (actual struct member name might differ)
        //     //       sens_cfg.cfg.any_motion.duration = YOUR_DURATION;
        //     ESP_LOGI(TAG, "Setting Any Motion feature configuration...");
        //     rslt = bmi2_set_sensor_config(&sens_cfg, 1, bmi2_dev);
        //     bmi2_error_codes_print_result(rslt);
        // }
        //
        // Using the provided set_feature_config:
        ESP_LOGI(TAG, "Setting Any Motion feature configuration using set_feature_config()...");
        rslt = set_feature_config(bmi2_dev); // This function must configure any-motion parameters
        bmi2_error_codes_print_result(rslt);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "set_feature_config for Any Motion failed.");
            return rslt;
        }

        // 6. Map Any Motion interrupt to a hardware interrupt pin (INT1 or INT2)
        struct bmi2_sens_int_config sens_int = { .type = BMI2_ANY_MOTION, .hw_int_pin = BMI2_INT1 };
        ESP_LOGI(TAG, "Mapping Any Motion feature interrupt to INT1...");
        // Assuming bmi270_map_feat_int is a wrapper for bmi2_map_feat_int
        rslt = bmi2_map_feat_int(&sens_int, 1, bmi2_dev);
        bmi2_error_codes_print_result(rslt);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "bmi2_map_feat_int for Any Motion failed.");
            return rslt;
        }

        ESP_LOGI(TAG, "BMI270 Any Motion detection initialized and configured.");
        ESP_LOGI(TAG, "Move the board to trigger Any Motion interrupt.");

        // 7. Create the handler task
        // Ensure task is not created multiple times if this function is called again
        static TaskHandle_t s_any_motion_task_handle = NULL;
        if (s_any_motion_task_handle == NULL) {
            BaseType_t task_created = xTaskCreate(
                any_motion_event_handler_task,
                "any_motion_task", // Task name
                4096,              // Stack size (adjust as needed)
                (void*)bmi2_dev,   // Pass bmi2_dev as parameter
                5,                 // Priority
                &s_any_motion_task_handle // Task handle
            );

            if (task_created != pdPASS) {
                ESP_LOGE(TAG, "Failed to create any_motion_task. Error code: %d", task_created);
                s_any_motion_task_handle = NULL; // Ensure handle is NULL on failure
                return BMI2_E_COM_FAIL; // Or a more specific error
            } else {
                ESP_LOGI(TAG, "any_motion_task created successfully.");
            }
        } else {
            ESP_LOGW(TAG, "any_motion_task already running or previously created.");
            // If task can be re-purposed, you might need to signal it or reset its state
        }
        
        return rslt; // Return the status of the last critical BMI2 API call
    }

    // --- Any Motion Event Handler Task ---
    static void any_motion_event_handler_task(void *pvParameters)
    {
        struct bmi2_dev *bmi2_dev = (struct bmi2_dev *)pvParameters;
        uint16_t int_status = 0;
        int8_t rslt;

        ESP_LOGI(TAG, "AnyMotionEventHandlerTask started. Waiting for motion...");

        while (1) {
            // Wait for the semaphore to be given by the ISR
            if (xSemaphoreTake(s_any_motion_semaphore, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "GPIO interrupt received for Any Motion.");

                // Interrupt received, now check BMI270 internal interrupt status
                rslt = bmi2_get_int_status(&int_status, bmi2_dev);
                if (rslt != BMI2_OK) {
                    ESP_LOGE(TAG, "Failed to get BMI270 interrupt status: %d", rslt);
                    bmi2_error_codes_print_result(rslt);
                    // Optionally, add a small delay before retrying or continuing
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }

                // Check if the "Any Motion" interrupt flag is set
                // BMI270_ANY_MOT_STATUS_MASK should be defined in bmi270.h or similar
                // e.g., #define BMI270_ANY_MOT_STATUS_MASK BMI2_ANY_MOT_INT_ASSERTED (or specific bit mask)
                // From BMI270 datasheet, for INT_STATUS_0, Any-motion is bit 0 when feature page is selected.
                // From bmi2_defs.h, BMI2_ANY_MOT_INT seems to be the status mask.
                if (int_status & BMI270_ANY_MOT_STATUS_MASK) { // Using BMI2_ANY_MOT_INT from bmi2_defs.h
                    ESP_LOGI(TAG, ">>> Any-motion event detected! Interrupt Status: 0x%04X", int_status);
                    // Add any specific actions needed upon detecting any motion
                    // For example, trigger an event, send a message, etc.

                    // Note: "Any motion" itself doesn't have detailed data like gesture ID.
                    // The event is the detection itself.
                } else {
                    // This case might occur if the GPIO interrupt was triggered by something else
                    // or if the interrupt status was cleared before this check.
                    ESP_LOGW(TAG, "GPIO interrupt received, but Any Motion status bit not set. Int_status: 0x%04X", int_status);
                }
                // No explicit clearing of interrupt status is usually needed for status registers,
                // as reading them often clears the latched bits. Check BMI270 datasheet if specific clearing is needed.

            }
            // The task will block on xSemaphoreTake, so no vTaskDelay is needed here for polling.
        }
    }

    void ImuEventHandlerTask() {
        ESP_LOGI(TAG, "ImuEventHandlerTask started.");
        uint16_t int_status = 0;
        struct bmi2_feat_sensor_data sens_data = { .type = BMI2_WRIST_GESTURE };
        const char* gesture_str[6] = {
            "unknown_gesture", "push_arm_down", "pivot_up",
            "wrist_shake_jiggle", "flick_in", "flick_out"
        };

        std::string wake_word="佩奇猪猪，我在逗你呢！";
        auto* led = static_cast<CircularStrip*>(this->GetLed()); // 获取 LED 对象
        auto &app = Application::GetInstance(); // 获取应用程序实例

        while (true) {
            // 1) 读取中断状态
            bmi2_get_int_status(&int_status, bmi_handle_);
            // 2) 如果检测到手势中断
            if (int_status & BMI270_WRIST_GEST_STATUS_MASK) {
                // 读取手势数据
                if (bmi270_get_feature_data(&sens_data, 1, bmi_handle_) == BMI2_OK) {
                    int id = sens_data.sens_data.wrist_gesture_output;
                    ESP_LOGI(TAG, "Detected gesture: %s (ID=%d)", gesture_str[id], id);
                    if (id >= 0 && id < 6) {
                        switch (id) {
                            case 0:
                                ESP_LOGI(TAG, "Action: Unknown gesture");
                                led->SetSingleColor(0, {0,   0,   0  });
                                break;
                            case 1:
                                app.ToggleChatState(); // 切换聊天状态
                                wake_word="佩奇猪猪，我把你抛到空中呢！飞翔的感觉怎么样？啊哈哈";
                                app.WakeWordInvoke(wake_word);
                                // ESP_LOGI(TAG, "Action: Push arm down");
                                // led->SetSingleColor(0, {255, 0,   0  });
                                break;
                            case 2:
                                ESP_LOGI(TAG, "Action: Pivot up");
                                led->SetSingleColor(0, {0,   255, 0  });
                                break;
                            case 3:
                                // ESP_LOGI(TAG, "Action: Wrist shake jiggle");
                                app.ToggleChatState(); // 切换聊天状态
                                wake_word="佩奇猪猪，我正在摇晃你哦！好好玩呢！啊哈哈";
                                app.WakeWordInvoke(wake_word);
                                // led->SetSingleColor(0, {0,   0,   255});
                                break;
                            case 4:
                                ESP_LOGI(TAG, "Action: Flick in");
                                led->SetSingleColor(0, {255, 255, 0  });
                                break;
                            case 5:
                                ESP_LOGI(TAG, "Action: Flick out");
                                led->SetSingleColor(0, {128, 0,   128});
                                break;
                        }
                    } else {
                        ESP_LOGW(TAG, "Unknown gesture ID: %d", id);
                    }
                } else {
                    ESP_LOGE(TAG, "ImuEventHandlerTask: failed to read feature data");
                }
                // 短暂显示后恢复常亮
                vTaskDelay(pdMS_TO_TICKS(500));
                led->SetSingleColor(0, {0, 0,  0});
            }
            // 
            // if (int_status & BMI2_TILT_INT) { // Replace with actual macro for Tilt interrupt
            //     // Handle Tilt
            //     // struct bmi2_sensor_data tilt_data;
            //     // tilt_data.type = BMI2_TILT;
            //     // bmi270_get_sensor_data(&tilt_data, 1, bmi_handle_); // Or bmi2_get_feature_data if tilt is a feature
            //     ESP_LOGI(TAG, "Tilt detected! Output: %d", tilt_data.sens_data.scalar_u16); // Example
            // }
            // if (int_status & BMI2_DOUBLE_TAP_INT) { 
            //     // Handle Double Tap
            //     ESP_LOGI(TAG, "Double Tap Detected!");
            //     // Double tap might not have specific "data" other than the event itself.
            // }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    void InitializeADC() { // 初始化 ADC
        adc_oneshot_unit_init_cfg_t init_config1 = { // ADC 单次采样单元初始化配置
            .unit_id = ADC_UNIT_1 // ADC 单元 ID
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle)); // 创建 ADC 单次采样单元

        adc_oneshot_chan_cfg_t chan_config = { // ADC 通道配置
            .atten = ADC_ATTEN, // 采样衰减
            .bitwidth = ADC_WIDTH, // 位宽
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC_CHANNEL, &chan_config)); // 配置 ADC 通道

        adc_cali_handle_t handle = NULL; // ADC 校准句柄
        esp_err_t ret = ESP_FAIL; // 返回值

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED // 如果支持曲线拟合校准方案
        adc_cali_curve_fitting_config_t cali_config = { // 曲线拟合校准配置
            .unit_id = ADC_UNIT_1, // ADC 单元 ID
            .atten = ADC_ATTEN, // 采样衰减
            .bitwidth = ADC_WIDTH, // 位宽
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle); // 创建曲线拟合校准方案
        if (ret == ESP_OK) {
            do_calibration = true; // 设置校准标志
            adc1_cali_handle = handle; // 设置 ADC 校准句柄
            ESP_LOGI(TAG, "ADC Curve Fitting calibration succeeded"); // 日志记录校准成功
        }
#endif // ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    }

    void InitializeButtons() { // 初始化按钮
        boot_button_.OnClick([this]() { // 启动按钮点击事件
            // auto &app = Application::GetInstance(); // 获取应用程序实例
            ResetWifiConfiguration(); // 重置 WiFi 配置
        });

        key_button_.OnClick([this]() { // 键按钮点击事件
            auto &app = Application::GetInstance(); // 获取应用程序实例
            app.ToggleChatState(); // 切换聊天状态
            key_long_pressed = false; // 重置键长按标志
        });

        key_button_.OnLongPress([this]() { // 键按钮长按事件
            int64_t now = esp_timer_get_time(); // 获取当前时间
            auto* led = static_cast<CircularStrip*>(this->GetLed()); // 获取 LED 对象

            if (key_long_pressed) { // 如果键已长按
                if ((now - last_key_press_time) < LONG_PRESS_TIMEOUT_US) { // 如果在超时时间内再次长按
                    ESP_LOGW(TAG, "Key button long pressed the second time within 5s, shutting down..."); // 日志记录第二次长按
                    led->SetSingleColor(0, {0, 0, 0}); // 设置 LED 颜色

                    gpio_hold_dis(MCU_VCC_CTL); // 禁用 GPIO 保持
                    gpio_set_level(MCU_VCC_CTL, 0); // 设置 GPIO 电平

                } else {
                    last_key_press_time = now; // 更新上次按键时间
                    BlinkGreenFor5s(); // 闪烁绿色 LED 5 秒
                }
                key_long_pressed = true; // 设置键长按标志
            } else { // 如果键未长按
                ESP_LOGW(TAG, "Key button first long press! Waiting second within 5s to shutdown..."); // 日志记录第一次长按
                last_key_press_time = now; // 更新上次按键时间
                key_long_pressed = true; // 设置键长按标志

                BlinkGreenFor5s(); // 闪烁绿色 LED 5 秒
            }
        });
    }

    void InitializePowerCtl() { // 初始化电源控制
        InitializeGPIO(); // 初始化 GPIO

        gpio_set_level(MCU_VCC_CTL, 1); // 设置 GPIO 电平
        gpio_hold_en(MCU_VCC_CTL); // 启用 GPIO 保持

        gpio_set_level(PERP_VCC_CTL, 1); // 设置 GPIO 电平
        gpio_hold_en(PERP_VCC_CTL); // 启用 GPIO 保持
    }

    void InitializeGPIO() { // 初始化 GPIO
        gpio_config_t io_pa = { // GPIO 配置
            .pin_bit_mask = (1ULL << AUDIO_CODEC_PA_PIN), // 引脚掩码
            .mode = GPIO_MODE_OUTPUT, // 模式
            .pull_up_en = GPIO_PULLUP_DISABLE, // 上拉使能
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉使能
            .intr_type = GPIO_INTR_DISABLE // 中断类型
        };
        gpio_config(&io_pa); // 配置 GPIO
        gpio_set_level(AUDIO_CODEC_PA_PIN, 0); // 设置 GPIO 电平

        gpio_config_t io_conf_1 = { // GPIO 配置
            .pin_bit_mask = (1ULL << MCU_VCC_CTL), // 引脚掩码
            .mode = GPIO_MODE_OUTPUT, // 模式
            .pull_up_en = GPIO_PULLUP_DISABLE, // 上拉使能
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉使能
            .intr_type = GPIO_INTR_DISABLE // 中断类型
        };
        gpio_config(&io_conf_1); // 配置 GPIO

        gpio_config_t io_conf_2 = { // GPIO 配置
            .pin_bit_mask = (1ULL << PERP_VCC_CTL), // 引脚掩码
            .mode = GPIO_MODE_OUTPUT, // 模式
            .pull_up_en = GPIO_PULLUP_DISABLE, // 上拉使能
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉使能
            .intr_type = GPIO_INTR_DISABLE // 中断类型
        };
        gpio_config(&io_conf_2); // 配置 GPIO
    }

    void InitializeIot() { // 初始化 IoT
        auto& thing_manager = iot::ThingManager::GetInstance(); // 获取 IoT 设备管理器实例
        thing_manager.AddThing(iot::CreateThing("Speaker")); // 添加 Speaker 设备
        thing_manager.AddThing(iot::CreateThing("Battery")); // 添加 Battery 设备
    }

    void BlinkGreenFor5s() { // 闪烁绿色 LED 5 秒
        auto* led = static_cast<CircularStrip*>(GetLed()); // 获取 LED 对象
        if (!led) { // 如果 LED 对象为空
            return; // 返回
        }

        led->Blink({50, 25, 0}, 100); // 闪烁 LED

        esp_timer_create_args_t timer_args = { // 定时器创建参数
            .callback = [](void* arg) { // 回调函数
                auto* self = static_cast<EspSpotS3Bot*>(arg); // 获取 EspSpotS3Bot 对象
                auto* led = static_cast<CircularStrip*>(self->GetLed()); // 获取 LED 对象
                if (led) { // 如果 LED 对象不为空
                    led->SetSingleColor(0, {0, 0, 0}); // 设置 LED 颜色
                }
            },
            .arg = this, // 参数
            .dispatch_method = ESP_TIMER_TASK, // 分发方法
            .name = "blinkGreenFor5s_timer" // 定时器名称
        };

        esp_timer_handle_t blink_timer = nullptr; // 定时器句柄
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &blink_timer)); // 创建定时器
        ESP_ERROR_CHECK(esp_timer_start_once(blink_timer, LONG_PRESS_TIMEOUT_US)); // 启动定时器
    }

public:

    EspSpotS3Bot() : boot_button_(BOOT_BUTTON_GPIO), key_button_(KEY_BUTTON_GPIO, true) {
        InitializePowerCtl(); // 初始化电源控制
        InitializeADC(); // 初始化 ADC
        InitializeI2c(); // 初始化 I2C
        InitializeButtons(); // 初始化按钮
        InitializeIot(); // 初始化 IoT
        // bmi270_enable_wrist_gesture();
        bmi270_enable_accel_gyro();
    }

    virtual Led* GetLed() override { // 获取 LED 对象
        static CircularStrip led(LED_PIN, 1); // 静态环形 LED 条对象
        return &led; // 返回 LED 对象
    }

    virtual AudioCodec* GetAudioCodec() override { // 获取音频编解码器对象
         static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, false);
        return &audio_codec; // 返回音频编解码器对象
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) { // 获取电池电量
        if (!adc1_handle) { // 如果 ADC 单次采样单元句柄为空
            InitializeADC(); // 初始化 ADC
        }

        int raw_value = 0; // 原始值
        int voltage = 0; // 电压

        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, VBAT_ADC_CHANNEL, &raw_value)); // 读取 ADC 值

        if (do_calibration) { // 如果进行校准
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_value, &voltage)); // 校准电压
            voltage = voltage * 3 / 2; // 补偿电压分压
            ESP_LOGI(TAG, "Calibrated voltage: %d mV", voltage); // 日志记录校准电压
        } else {
            ESP_LOGI(TAG, "Raw ADC value: %d", raw_value); // 日志记录原始 ADC 值
            voltage = raw_value; // 设置电压
        }

        voltage = voltage < EMPTY_BATTERY_VOLTAGE ? EMPTY_BATTERY_VOLTAGE : voltage; // 限制电压下限
        voltage = voltage > FULL_BATTERY_VOLTAGE ? FULL_BATTERY_VOLTAGE : voltage; // 限制电压上限

        // 计算电量百分比
        level = (voltage - EMPTY_BATTERY_VOLTAGE) * 100 / (FULL_BATTERY_VOLTAGE - EMPTY_BATTERY_VOLTAGE);

        charging = gpio_get_level(MCU_VCC_CTL); // 获取充电状态
        ESP_LOGI(TAG, "Battery Level: %d%%, Charging: %s", level, charging ? "Yes" : "No"); // 日志记录电池电量和充电状态
        return true; // 返回成功
    }
};

DECLARE_BOARD(EspSpotS3Bot); // 声明 EspSpotS3Bot 板
