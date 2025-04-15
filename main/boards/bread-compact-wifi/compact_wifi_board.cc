#include "wifi_board.h"
#include "audio_codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

class CompactWifiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_; // 声明一个I2C总线句柄，用于管理I2C通信。
    esp_lcd_panel_io_handle_t panel_io_ = nullptr; // 声明一个LCD面板IO句柄，用于管理LCD面板的I2C通信。
    esp_lcd_panel_handle_t panel_ = nullptr; // 声明一个LCD面板句柄，用于管理LCD面板的显示操作。
    Display* display_ = nullptr; // 声明一个显示对象，用于管理显示操作。
    Button boot_button_; // 声明一个启动按钮对象，用于管理启动按钮的点击事件。
    Button touch_button_; // 声明一个触摸按钮对象，用于管理触摸按钮的点击事件。
    Button volume_up_button_; // 声明一个音量增加按钮对象，用于管理音量增加按钮的点击事件。
    Button volume_down_button_; // 声明一个音量减少按钮对象，用于管理音量减少按钮的点击事件。

    // 初始化显示I2C总线
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0, // 使用I2C端口0
            .sda_io_num = DISPLAY_SDA_PIN, // 设置SDA（数据线）引脚
            .scl_io_num = DISPLAY_SCL_PIN, // 设置SCL（时钟线）引脚
            .clk_source = I2C_CLK_SRC_DEFAULT, // 使用默认的时钟源
            .glitch_ignore_cnt = 7, // 忽略7个毛刺
            .intr_priority = 0, // 中断优先级设置为0
            .trans_queue_depth = 0, // 传输队列深度为0
            .flags = {
                .enable_internal_pullup = 1, // 启用内部上拉电阻，确保信号线在空闲时保持高电平。
            },
        };
        // 创建I2C总线，i2c_new_master_bus函数创建一个新的I2C主总线，并将其句柄存储在display_i2c_bus_中。
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_)); 
    }

    // 初始化SSD1306显示
    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C, // 设置I2C设备的地址，0x3C是SSD1306的默认地址。
            .on_color_trans_done = nullptr, // 设置颜色传输完成回调函数，这里为空。
            .user_ctx = nullptr, // 设置用户上下文，这里为空。
            .control_phase_bytes = 1, // 设置控制阶段字节数，这里为1。
            .dc_bit_offset = 6, // 设置数据/命令选择位偏移，这里为6。
            .lcd_cmd_bits = 8, // 设置LCD命令位宽，这里为8。
            .lcd_param_bits = 8, // 设置LCD参数位宽，这里为8。
            .flags = {
                .dc_low_on_data = 0, // 设置数据/命令选择位偏移，这里为6。
                .disable_control_phase = 0, // 设置LCD命令位宽，这里为8。
            },
            .scl_speed_hz = 400 * 1000, // 设置I2C时钟速度，这里为400kHz。
        };
        // 创建LCD面板IO句柄，esp_lcd_new_panel_io_i2c_v2函数创建显示面板的I2C IO。
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1; // 设置复位引脚，这里为-1，表示没有复位引脚。
        panel_config.bits_per_pixel = 1; // 设置每个像素的位数，这里为1。

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT), // 设置显示高度，这里为DISPLAY_HEIGHT。
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // 重置显示面板
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        // 初始化显示面板
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        // 打开显示面板
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
        // 创建OLED显示对象
        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    // 初始化按钮及其回调函数
    void InitializeButtons() {
        // 启动按钮点击事件处理
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // 如果设备处于启动状态且WiFi未连接,则重置WiFi配置
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            // 切换聊天状态
            app.ToggleChatState();
        });

        // 触摸按钮按下事件 - 开始监听
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });

        // 触摸按钮释放事件 - 停止监听
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        // 音量增加按钮点击事件
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            // 增加音量10个单位,最大不超过100
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            // 显示音量通知
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        // 音量增加按钮长按事件 - 设置最大音量
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        // 音量减少按钮点击事件
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            // 减少音量10个单位,最小不低于0
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            // 显示音量通知
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        // 音量减少按钮长按事件 - 静音
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        // 获取物联网管理器实例
        auto& thing_manager = iot::ThingManager::GetInstance();
        // 添加扬声器设备
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        // 添加灯设备
        thing_manager.AddThing(iot::CreateThing("Lamp"));
    }

public:
    // 构造函数
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeIot();
    }

    // 获取LED实例
    virtual Led* GetLed() override {
        // 创建一个单个LED实例
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    // 获取音频编解码器实例
    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    // 获取显示实例
    virtual Display* GetDisplay() override {
        return display_;
    }
};

// 声明CompactWifiBoard类为板子类
DECLARE_BOARD(CompactWifiBoard);
