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

// #include "driver/i2c_bus.h"         // i2c_bus_create, i2c_bus_handle_t
#include "bmi270.h"                  // espressif2022/bmi270 API
#include "common/common.h"

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

    // I2C read function for BMI270
    static BMI2_INTF_RETURN_TYPE bmi270_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
    {
        if ((reg_data == NULL) || (len == 0) || (len > 32)) {
            return BMI2_E_COM_FAIL;
        }

        esp_err_t ret;
        i2c_master_dev_handle_t i2c_dev = *((i2c_master_dev_handle_t*)intf_ptr);

        // Read data from register
        ret = i2c_master_transmit_receive(i2c_dev, &reg_addr, 1, reg_data, len, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C read failed: 0x%02X, error=%d", reg_addr, ret);
            return BMI2_E_COM_FAIL;
        }

        return BMI2_OK;
    }

    // I2C write function for BMI270
    static BMI2_INTF_RETURN_TYPE bmi270_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
    {
        if ((reg_data == NULL) || (len == 0) || (len > 32)) {
            return BMI2_E_COM_FAIL;
        }

        esp_err_t ret;
        i2c_master_dev_handle_t i2c_dev = *((i2c_master_dev_handle_t*)intf_ptr);

        // Create temporary buffer for register + data
        uint8_t write_buf[33]; // reg_addr(1) + data(up to 32)
        write_buf[0] = reg_addr;
        memcpy(&write_buf[1], reg_data, len);

        // Write data to register
        ret = i2c_master_transmit(i2c_dev, write_buf, len + 1, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C write failed: 0x%02X, error=%d", reg_addr, ret);
            return BMI2_E_COM_FAIL;
        }

        return BMI2_OK;
    }

    // Delay function for BMI270
    static void bmi270_delay_us(uint32_t period, void *intf_ptr)
    {
        (void)intf_ptr; // Unused parameter
        
        // Convert microseconds to milliseconds and delay
        if (period < 1000) {
            // For small delays, use esp_rom_delay_us
            esp_rom_delay_us(period);
        } else {
            // For longer delays, use vTaskDelay
            vTaskDelay(pdMS_TO_TICKS(period / 1000));
        }
    }

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
        ESP_LOGI(TAG, "InitializeBmi270: Entry. i2c_bus_ (high-level) handle: %p", this->i2c_bus_);
        ESP_LOGI(TAG, "GetAudioCodec: Entry. i2c_bus_ (high-level) handle before static audio_codec init: %p", this->i2c_bus_);
    }

    void InitializeBmi270() {
        ESP_LOGI(TAG, "InitializeBmi270 (low-level espressif2022/bmi270)…");

        // 1) 创建并配置底层 I2C 总线（如果还没创建的话）
        if (!lowlevel_i2c_bus_) {
            i2c_config_t bus_conf;
            memset(&bus_conf, 0, sizeof(bus_conf));
            bus_conf.mode           = I2C_MODE_MASTER;
            bus_conf.sda_io_num     = I2C_MASTER_SDA_PIN;
            bus_conf.sda_pullup_en  = GPIO_PULLUP_ENABLE;
            bus_conf.scl_io_num     = I2C_MASTER_SCL_PIN;
            bus_conf.scl_pullup_en  = GPIO_PULLUP_ENABLE;
            bus_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;  // 100kHz
            lowlevel_i2c_bus_ = i2c_bus_create(I2C_NUM_0, &bus_conf);
            if (!lowlevel_i2c_bus_) {
                ESP_LOGE(TAG, "InitializeBmi270: low-level I2C bus create failed");
                return;
            }
            ESP_LOGI(TAG, "Low-level I2C bus created: %p", lowlevel_i2c_bus_);
        }

        // 2) 创建 BMI270 传感器对象
        bmi270_i2c_config_t bmi_conf = {
            .i2c_handle = lowlevel_i2c_bus_,
            .i2c_addr   = BMI270_I2C_ADDRESS,  // 0x68 或 0x69
        };
        esp_err_t err = bmi270_sensor_create(&bmi_conf, &bmi_handle_);
        if (err != ESP_OK || !bmi_handle_) {
            ESP_LOGE(TAG, "InitializeBmi270: bmi270_sensor_create failed (%d)", err);
            return;
        }
        ESP_LOGI(TAG, "BMI270 sensor handle: %p", bmi_handle_);

        // 3) 使能加速度计 + 手势传感器
        uint8_t sens_list[] = { BMI2_ACCEL, BMI2_WRIST_GESTURE };
        bmi270_sensor_enable(sens_list, sizeof(sens_list), bmi_handle_);

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

        err = bmi270_map_feat_int(&int_cfg, 1, this->bmi_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bmi270_map_feat_int failed: %s", esp_err_to_name(err));
            return;
        }

        ESP_LOGI(TAG, "BMI270 initialized and configured.");

        BaseType_t task_created = xTaskCreate(
            [](void* arg) { static_cast<EspSpotS3Bot*>(arg)->GestureTask(); },
            "gesture_task", 4096, this, 5, NULL );

        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create gesture_task. Error code: %d", task_created);
        } else {
            ESP_LOGI(TAG, "gesture_task created successfully.");
        }

    }

    void GestureTask() {
        ESP_LOGI(TAG, "GestureTask started.");
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
                    ESP_LOGE(TAG, "GestureTask: failed to read feature data");
                }
                // 短暂显示后恢复常亮
                vTaskDelay(pdMS_TO_TICKS(500));
                led->SetSingleColor(0, {0, 0,  0});
            }

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
        InitializeBmi270();
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
