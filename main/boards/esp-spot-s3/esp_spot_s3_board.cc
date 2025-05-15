#include "wifi_board.h" // 包含 WiFi 板的头文件
#include "audio_codecs/es8311_audio_codec.h" // 包含 ES8311 音频编解码器的头文件
#include "application.h" // 包含应用程序的头文件
#include "button.h" // 包含按钮的头文件
#include "config.h" // 包含配置的头文件
#include "iot/thing_manager.h" // 包含 IoT 设备管理器的头文件
#include "sdkconfig.h" // 包含 SDK 配置的头文件

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

#define TAG "esp_spot_s3" // 定义日志标签

bool button_released_ = false; // 按钮释放标志
bool shutdown_ready_ = false; // 关机准备标志
esp_timer_handle_t shutdown_timer; // 关机定时器句柄

class EspSpotS3Bot : public WifiBoard { // 定义 EspSpotS3Bot 类，继承自 WifiBoard
private:
    i2c_master_bus_handle_t i2c_bus_; // I2C 总线句柄
    Button boot_button_; // 启动按钮对象
    Button key_button_; // 键按钮对象
    adc_oneshot_unit_handle_t adc1_handle; // ADC 单次采样单元句柄
    adc_cali_handle_t adc1_cali_handle; // ADC 校准句柄
    bool do_calibration = false; // 是否进行校准标志
    bool key_long_pressed = false; // 键长按标志
    int64_t last_key_press_time = 0; // 上次按键时间
    static const int64_t LONG_PRESS_TIMEOUT_US = 5 * 1000000ULL; // 长按超时时间

    void InitializeI2c() { // 初始化 I2C
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = { // I2C 总线配置
            .i2c_port = I2C_NUM_0, // I2C 端口
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN, // SDA 引脚
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN, // SCL 引脚
            .clk_source = I2C_CLK_SRC_DEFAULT, // 时钟源
            .glitch_ignore_cnt = 7, // 忽略毛刺计数
            .intr_priority = 0, // 中断优先级
            .trans_queue_depth = 0, // 传输队列深度
            .flags = {
                .enable_internal_pullup = 1, // 启用内部上拉
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_)); // 创建 I2C 主机总线
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
            auto&amp; app = Application::GetInstance(); // 获取应用程序实例
            ResetWifiConfiguration(); // 重置 WiFi 配置
        });

        key_button_.OnClick([this]() { // 键按钮点击事件
            auto&amp; app = Application::GetInstance(); // 获取应用程序实例
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
        gpio_config(&amp;io_pa); // 配置 GPIO
        gpio_set_level(AUDIO_CODEC_PA_PIN, 0); // 设置 GPIO 电平

        gpio_config_t io_conf_1 = { // GPIO 配置
            .pin_bit_mask = (1ULL << MCU_VCC_CTL), // 引脚掩码
            .mode = GPIO_MODE_OUTPUT, // 模式
            .pull_up_en = GPIO_PULLUP_DISABLE, // 上拉使能
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉使能
            .intr_type = GPIO_INTR_DISABLE // 中断类型
        };
        gpio_config(&amp;io_conf_1); // 配置 GPIO

        gpio_config_t io_conf_2 = { // GPIO 配置
            .pin_bit_mask = (1ULL << PERP_VCC_CTL), // 引脚掩码
            .mode = GPIO_MODE_OUTPUT, // 模式
            .pull_up_en = GPIO_PULLUP_DISABLE, // 上拉使能
            .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉使能
            .intr_type = GPIO_INTR_DISABLE // 中断类型
        };
        gpio_config(&amp;io_conf_2); // 配置 GPIO
    }

    void InitializeIot() { // 初始化 IoT
        auto&amp; thing_manager = iot::ThingManager::GetInstance(); // 获取 IoT 设备管理器实例
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
        ESP_ERROR_CHECK(esp_timer_create(&amp;timer_args, &amp;blink_timer)); // 创建定时器
        ESP_ERROR_CHECK(esp_timer_start_once(blink_timer, LONG_PRESS_TIMEOUT_US)); // 启动定时器
    }

public:
    EspSpotS3Bot() : boot_button_(BOOT_BUTTON_GPIO), key_button_(KEY_BUTTON_GPIO, true) { // 构造函数
        InitializePowerCtl(); // 初始化电源控制
        InitializeADC(); // 初始化 ADC
        InitializeI2c(); // 初始化 I2C
        InitializeButtons(); // 初始化按钮
        InitializeIot(); // 初始化 IoT
    }

    virtual Led* GetLed() override { // 获取 LED 对象
        static CircularStrip led(LED_PIN, 1); // 静态环形 LED 条对象
        return &amp;led; // 返回 LED 对象
    }

    virtual AudioCodec* GetAudioCodec() override { // 获取音频编解码器对象
         static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, // 静态 ES8311 音频编解码器对象
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, false);
        return &amp;audio_codec; // 返回音频编解码器对象
    }

    virtual bool GetBatteryLevel(int &amp;level, bool &amp;charging, bool &amp;discharging) { // 获取电池电量
        if (!adc1_handle) { // 如果 ADC 单次采样单元句柄为空
            InitializeADC(); // 初始化 ADC
        }

        int raw_value = 0; // 原始值
        int voltage = 0; // 电压

        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, VBAT_ADC_CHANNEL, &amp;raw_value)); // 读取 ADC 值

        if (do_calibration) { // 如果进行校准
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_value, &amp;voltage)); // 校准电压
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
