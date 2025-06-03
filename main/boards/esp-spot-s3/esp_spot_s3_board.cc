#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "sdkconfig.h"
#include <string.h>
#include "driver/gpio.h"
#include "freertos/task.h"
#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "led/circular_strip.h"

#include "bmi270_manager.h"

#define TAG "esp_spot_s3" // 定义日志标签

bool button_released_ = false; // 按钮释放标志
bool shutdown_ready_ = false; // 关机准备标志
esp_timer_handle_t shutdown_timer; // 关机定时器句柄

class EspSpotS3Bot : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;// I2C 总线句柄
    i2c_bus_handle_t lowlevel_i2c_bus_ = nullptr; // —— 新增：底层 I2C，用于 bmi270 ——  
    bmi270_handle_t bmi_handle_ = nullptr; // BMI270 句柄
    Button boot_button_; // 启动按钮对象
    Button key_button_; // 键按钮对象
    adc_oneshot_unit_handle_t adc1_handle; // ADC 单次采样单元句柄
    adc_cali_handle_t adc1_cali_handle; // ADC 校准句柄
    bool do_calibration = false; // 是否进行校准标志
    bool key_long_pressed = false; // 键长按标志
    int64_t last_key_press_time = 0; // 上次按键时间
    static const int64_t LONG_PRESS_TIMEOUT_US = 3 * 1000000ULL; // 长按超时时间
    // BMI270管理器组件
    Bmi270Manager bmi270_manager_; 
    
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
        
        ESP_LOGI(TAG, "GetAudioCodec: Entry. i2c_bus_ (high-level) handle before static audio_codec init: %p", this->i2c_bus_);
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

    bool InitBMI270Imu() {

        i2c_config_t bus_conf;
        memset(&bus_conf, 0, sizeof(bus_conf));
        bus_conf.mode = I2C_MODE_MASTER;
        bus_conf.sda_io_num = I2C_MASTER_SDA_PIN;
        bus_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        bus_conf.scl_io_num = I2C_MASTER_SCL_PIN;
        bus_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        bus_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
        this->lowlevel_i2c_bus_ = i2c_bus_create(I2C_NUM_0, &bus_conf);
        if (!this->lowlevel_i2c_bus_) {
            ESP_LOGE(TAG, "Low-level I2C bus (for BMI270) creation failed!");
            abort(); 
        } else {
            ESP_LOGI(TAG, "Low-level I2C bus (lowlevel_i2c_bus_ for BMI270) created: %p", this->lowlevel_i2c_bus_);
        }
        
        // 2) 创建 BMI270 传感器对象
        bmi270_i2c_config_t i2c_bmi270_conf = {
            .i2c_handle = lowlevel_i2c_bus_,
            .i2c_addr = BMI270_I2C_ADDRESS,
        };
        esp_err_t err = bmi270_sensor_create(&i2c_bmi270_conf, &bmi_handle_);
        if (err != ESP_OK || !bmi_handle_) {
            ESP_LOGE(TAG, "bmi270_sensor_create failed (%d)", err);
            return false;
        }
        ESP_LOGI(TAG, "BMI270 sensor handle: %p", bmi_handle_);

        // 初始化BMI270，启用任意运动、手势识别、高g、低g
        Bmi270Manager::Config bmi_conf;
        // bmi_conf.features = Bmi270Manager::HIGH_G;
        // bmi_conf.features = Bmi270Manager::LOW_G;
        bmi_conf.features = Bmi270Manager::WRIST_GESTURE;
        // bmi_conf.features = Bmi270Manager::WRIST_GESTURE | Bmi270Manager::HIGH_G | Bmi270Manager::LOW_G;
        bmi_conf.int_pin = I2C_INT_IO;  // 使用config.h中定义的 I2C_INT_IO 
        // 设置BMI270设备句柄
        bmi270_manager_.bmi_dev_ = bmi_handle_;
        // 设置回调函数
        bmi270_manager_.SetAnyMotionCallback([this]() { this->OnAnyMotion(); });
        bmi270_manager_.SetWristGestureCallback([this](int id) { this->OnWristGesture(id); });
        bmi270_manager_.SetAccelGyroCallback([this](float ax, float ay, float az, float gx, float gy, float gz) {
            this->OnAccelGyroData(ax, ay, az, gx, gy, gz);
        });
        // 设置高g/低g回调，增加详细日志定位问题
        bmi270_manager_.SetHighGCallback([this](uint8_t high_g_out) {
            ESP_LOGI(TAG, "[EspSpotS3Bot] High-G callback triggered, will call OnHighG");
            this->OnHighG(high_g_out);
        });
        bmi270_manager_.SetLowGCallback([this]() {
            ESP_LOGI(TAG, "[EspSpotS3Bot] Low-G callback triggered, will call OnLowG");
            this->OnLowG();
        });
        // 初始化BMI270管理器
        if (!bmi270_manager_.Init(bmi_conf)) {
            ESP_LOGE(TAG, "Failed to initialize BMI270 manager");
            return false;
        }
        return true;
    }

public:
    EspSpotS3Bot() : boot_button_(BOOT_BUTTON_GPIO), key_button_(KEY_BUTTON_GPIO, true) {
        InitializePowerCtl();
        InitializeADC();
        InitializeI2c();
        InitializeButtons();
        InitializeIot();
        if (!InitBMI270Imu()) {
            ESP_LOGE(TAG, "Failed to initialize IMU");
            return;
        }
    }

    virtual Led* GetLed() override {
        static CircularStrip led(LED_PIN, 1);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, false);
        return &audio_codec;
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

    void OnAnyMotion() {
        ESP_LOGI(TAG, "[EspSpotS3Bot] Any Motion Event: 控制灯光/唤醒词等");
        auto* led = static_cast<CircularStrip*>(GetLed());
        if (led) led->SetSingleColor(0, {0,255,0});
        // 这里可以添加唤醒词、控制电源等
    }

    void OnWristGesture(int gesture_id) {
        const char* gesture_name = (gesture_id >= 0 && gesture_id < 6) ? 
            Bmi270Manager::GESTURE_OUTPUT_STRINGS[gesture_id] : "invalid_gesture";
        ESP_LOGI(TAG, "[EspSpotS3Bot]  Wrist Gesture detected: %s (id: %d)", gesture_name, gesture_id);

        std::string wake_word="佩奇猪猪，我在逗你呢！";
        auto* led = static_cast<CircularStrip*>(this->GetLed()); // 获取 LED 对象
        auto &app = Application::GetInstance(); // 获取应用程序实例
        switch (gesture_id) {
            case 0:
                ESP_LOGI(TAG, "Action: Unknown gesture");
                led->SetSingleColor(0, {0,   0,   0  });
                break;
            case 1: // push_arm_down
                app.ToggleChatState(); // 切换聊天状态
                wake_word="佩奇猪猪，我把你抓在我手上用力往下甩起来咯！失重的感觉好玩吗，像不像跳楼机？";
                app.WakeWordInvoke(wake_word);
                led->SetSingleColor(0, {255, 0,   0  });
                break;
            case 2: // pivot_up
                app.ToggleChatState(); // 切换聊天状态
                wake_word="佩奇猪猪，我把你抓在我手上用力往上甩起来咯！超重的感觉好玩吗?";
                app.WakeWordInvoke(wake_word);
                led->SetSingleColor(0, {0,   255, 0  });
                break;
            case 3: // wrist_shake_jiggle
                app.ToggleChatState(); // 切换聊天状态
                wake_word="佩奇猪猪，我正在左右摇晃你呀！摇晃的感觉怎么样，晕不晕哦，哈哈哈哈！";
                app.WakeWordInvoke(wake_word);
                led->SetSingleColor(0, {0,   0, 255});
                break;
            case 4: // flick_in
                app.ToggleChatState(); // 切换聊天状态
                wake_word="佩奇猪猪，我正在快速地把你拉回来哦，不要淘气走掉啦，哈哈哈哈！";
                app.WakeWordInvoke(wake_word);
                led->SetSingleColor(0, {255, 255, 0  });
                break;
            case 5: // flick_out
                app.ToggleChatState(); // 切换聊天状态
                wake_word="佩奇猪猪，我正在快速地把你推出去啦，你怕不怕呀，哈哈哈哈！";
                app.WakeWordInvoke(wake_word);
                led->SetSingleColor(0, {128, 0,   128});
                break;
            // default ：
            //     break;
        }
    }

    void OnAccelGyroData(float acc_x, float acc_y, float acc_z, float gyr_x, float gyr_y, float gyr_z) {
        ESP_LOGI(TAG, "[EspSpotS3Bot] AccelGyroData Event: acc_x=%f, acc_y=%f, acc_z=%f, gyr_x=%f, gyr_y=%f, gyr_z=%f",
                 acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z);
        // 可选：处理加速度/角速度数据
    }

    void OnHighG(uint8_t high_g_out) {
        ESP_LOGI(TAG, "[EspSpotS3Bot] OnHighG: Output=0x%x", high_g_out);
        auto* led = static_cast<CircularStrip*>(GetLed());
        if (led) led->SetSingleColor(0, {255, 128, 0}); // 橙色
        std::string msg = "佩奇猪猪，检测到你被猛地往上甩啦！小心飞起来哦！";
        auto &app = Application::GetInstance();
        app.WakeWordInvoke(msg);
    }

    void OnLowG() {
        ESP_LOGI(TAG, "[EspSpotS3Bot] OnLowG: Free fall detected!");
        auto* led = static_cast<CircularStrip*>(GetLed());
        if (led) led->SetSingleColor(0, {0, 255, 255}); // 青色
        std::string msg = "佩奇猪猪，检测到你在做自由落体啦！要摔倒了，快叫我接住你把！";
        auto &app = Application::GetInstance();
        app.WakeWordInvoke(msg);
    }

};
DECLARE_BOARD(EspSpotS3Bot);
