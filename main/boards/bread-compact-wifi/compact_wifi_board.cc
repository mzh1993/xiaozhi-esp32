#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "touch_button_wrapper.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "fan_controller.h"
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
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;           
    Button volume_up_button_;       
    Button volume_down_button_;     

    // 新增玩具触摸按键
    TouchButtonWrapper head_touch_button_;
    TouchButtonWrapper hand_touch_button_;
    TouchButtonWrapper belly_touch_button_;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    void InitializeTouchSensor() {
        // 初始化触摸传感器 - 传入所有需要的通道
        uint32_t touch_channels[] = {TOUCH_CHANNEL_HEAD, TOUCH_CHANNEL_HAND, TOUCH_CHANNEL_BELLY};
        int channel_count = sizeof(touch_channels) / sizeof(touch_channels[0]);
        TouchButtonWrapper::InitializeTouchSensor(touch_channels, channel_count);
        TouchButtonWrapper::StartTouchSensor();
        
        // 触摸传感器初始化完成后，创建所有按钮
        head_touch_button_.CreateButton();
        hand_touch_button_.CreateButton();
        belly_touch_button_.CreateButton();
        
        ESP_LOGI(TAG, "Touch sensor initialized for toy touch buttons");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        
        // 保留原来的实体按键事件处理
        touch_button_.OnPressDown([this]() {
            ESP_LOGI(TAG, "Touch button pressed down");
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            ESP_LOGI(TAG, "Touch button pressed up");
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Volume up button clicked");
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Volume up button long pressed");
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Volume down button clicked");
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Volume down button long pressed");
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
        
        // 新增玩具触摸按键事件处理 - 使用新的事件接口
        head_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Head touch button clicked");
            GetDisplay()->ShowNotification("摸摸头~");
            // 使用新的事件接口，只发送事件，不调用业务逻辑
            Application::GetInstance().PostTouchEvent("摸摸头~");
        });
        
        head_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Head touch button long pressed");
            GetDisplay()->ShowNotification("长时间摸头~");
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent("长时间摸头~");
        });
        
        hand_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Hand touch button clicked");
            GetDisplay()->ShowNotification("握手手~");
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent("我们来握手手哦！");
        });
        
        hand_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Hand touch button long pressed");
            GetDisplay()->ShowNotification("我要抢你手上的玩具咯~");
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent("我要抢你手上的玩具咯");
        });
        
        belly_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Belly touch button clicked");
            GetDisplay()->ShowNotification("摸摸肚子~");
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent("摸摸肚子~");
        });
        
        belly_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Belly touch button long pressed");
            GetDisplay()->ShowNotification("长时间摸肚子~");
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent("长时间摸肚子~");
        });

    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        static FanController fan(FAN_GPIO);  // 添加风扇控制器  
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),           // 保留原来的实体按键
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),   // 保留原来的实体按键
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO), // 保留原来的实体按键
        head_touch_button_(TOUCH_CHANNEL_HEAD, 0.10f),    // 新增玩具头部触摸，降低阈值提高灵敏度
        hand_touch_button_(TOUCH_CHANNEL_HAND, 0.10f),    // 新增玩具手部触摸，降低阈值提高灵敏度
        belly_touch_button_(TOUCH_CHANNEL_BELLY, 0.10f) { // 新增玩具肚子触摸，降低阈值提高灵敏度
        
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeTouchSensor();  // 先初始化触摸传感器
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

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

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
