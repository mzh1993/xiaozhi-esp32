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
// #include "led188_display.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <chrono>
#include <random>

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
    Button fan_button_;           
    Button volume_up_button_;       
    Button volume_down_button_;     

    // 新增玩具触摸按键
    TouchButtonWrapper head_touch_button_;
    TouchButtonWrapper hand_touch_button_;
    TouchButtonWrapper belly_touch_button_;
    
    // 风扇控制器
    FanController* fan_controller_ = nullptr;
    
    // 188数码管显示
    // Led188Display* led188_display_ = nullptr;

    // 触摸按键文本候选列表
    std::vector<std::string> head_touch_texts_ = {
        "摸摸头~",
        "好舒服的头~",
        "摸摸你的小脑袋",
        "头好痒痒的",
        "摸摸头，摸摸头",
        "你的头发软软的",
        "摸摸你的头发",
        "头好温暖",
        "摸摸你的小脑瓜",
        "头好舒服"
    };
    
    std::vector<std::string> hand_touch_texts_ = {
        "我们来握手手哦！",
        "握手手，好朋友",
        "你的手好温暖",
        "握手手，拉拉手",
        "我们来击掌吧！",
        "握手手，一起玩",
        "你的手好软",
        "握手手，好朋友",
        "我们来拉拉手",
        "握手手，真开心"
    };
    
    std::vector<std::string> belly_touch_texts_ = {
        "摸摸肚子~",
        "肚子好痒痒",
        "摸摸你的小肚子",
        "肚子好软软的",
        "摸摸肚子，好舒服",
        "你的肚子圆圆的",
        "摸摸你的小肚皮",
        "肚子好温暖",
        "摸摸肚子，真开心",
        "肚子好舒服"
    };
    
    std::vector<std::string> head_long_press_texts_ = {
        "长时间摸头~",
        "摸头摸了好久",
        "头被摸得好舒服",
        "长时间摸摸头",
        "头被摸得痒痒的",
        "摸头摸得停不下来",
        "头被摸得好温暖",
        "长时间摸摸小脑袋",
        "头被摸得好开心",
        "摸头摸得好久"
    };
    
    std::vector<std::string> hand_long_press_texts_ = {
        "我要抢你手上的玩具咯",
        "你的玩具看起来好好玩",
        "我也想玩你的玩具",
        "玩具让我看看",
        "你的玩具好有趣",
        "我也想摸摸玩具",
        "玩具让我玩玩",
        "你的玩具好漂亮",
        "我也想玩一下",
        "玩具让我试试"
    };
    
    std::vector<std::string> belly_long_press_texts_ = {
        "长时间摸肚子~",
        "肚子被摸了好久",
        "摸肚子摸得停不下来",
        "肚子被摸得好舒服",
        "长时间摸摸肚子",
        "肚子被摸得痒痒的",
        "摸肚子摸得好久",
        "肚子被摸得好温暖",
        "长时间摸摸小肚子",
        "肚子被摸得好开心"
    };

    // 随机选择文本的辅助函数
    std::string GetRandomText(const std::vector<std::string>& texts) {
        if (texts.empty()) {
            return "摸摸你哦~";
        }
        // 使用更好的随机数生成器
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, texts.size() - 1);
        return texts[dis(gen)];
    }

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
        
        // 风扇按键事件处理 - 只处理单击和长按，避免重复触发
        fan_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Fan button clicked");
            if (fan_controller_) {
                fan_controller_->HandleButtonPress();
            }
        });
        
        fan_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Fan button long pressed");
            if (fan_controller_) {
                fan_controller_->HandleButtonLongPress();
            }
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
            GetDisplay()->ShowNotification(GetRandomText(head_touch_texts_));
            // 使用新的事件接口，只发送事件，不调用业务逻辑
            Application::GetInstance().PostTouchEvent(GetRandomText(head_touch_texts_));
        });
        
        head_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Head touch button long pressed");
            GetDisplay()->ShowNotification(GetRandomText(head_long_press_texts_));
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(GetRandomText(head_long_press_texts_));
        });
        
        hand_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Hand touch button clicked");
            GetDisplay()->ShowNotification(GetRandomText(hand_touch_texts_));
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(GetRandomText(hand_touch_texts_));
        });
        
        hand_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Hand touch button long pressed");
            GetDisplay()->ShowNotification(GetRandomText(hand_long_press_texts_));
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(GetRandomText(hand_long_press_texts_));
        });
        
        belly_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Belly touch button clicked");
            GetDisplay()->ShowNotification(GetRandomText(belly_touch_texts_));
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(GetRandomText(belly_touch_texts_));
        });
        
        belly_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Belly touch button long pressed");
            GetDisplay()->ShowNotification(GetRandomText(belly_long_press_texts_));
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(GetRandomText(belly_long_press_texts_));
        });

    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        
        // 初始化188数码管显示 (5线动态寻址) - 暂时屏蔽
        // led188_display_ = new Led188Display(LED188_PIN1_GPIO, LED188_PIN2_GPIO, LED188_PIN3_GPIO, 
        //                                    LED188_PIN4_GPIO, LED188_PIN5_GPIO);
        // ESP_LOGI(TAG, "LED188 display initialized in board");
        
        // 初始化风扇控制器
        fan_controller_ = new FanController(FAN_BUTTON_GPIO, FAN_GPIO, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "Fan controller initialized in board");
        
        // 将188数码管显示关联到风扇控制器 - 暂时屏蔽
        // if (fan_controller_ && led188_display_) {
        //     fan_controller_->SetLed188Display(led188_display_);
        //     ESP_LOGI(TAG, "LED188 display linked to fan controller");
        // }
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        fan_button_(FAN_BUTTON_GPIO),           
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
    
    virtual FanController* GetFanController() override {
        return fan_controller_;
    }
};

DECLARE_BOARD(CompactWifiBoard);
