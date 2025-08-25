#include "wifi_board.h"
// #include "toy_audio_codec_lite.h"
#include "codecs/es8311_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "touch_button_wrapper.h"
#include "config.h"
#include "power_save_timer.h"
#include "../common/lamp_controller.h"
#include "../common/fan_controller.h"
#include "led/single_led.h"
#include "../ear/tc118s_ear_controller.h"
#include "../ear/no_ear_controller.h"
#include "assets/lang_config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <chrono>
#include <random>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include <esp_timer.h>

#define TAG "AstronautToysESP32S3"

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

class AstronautToysESP32S3 : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button key1_button_;
    Button key2_button_;
    // 添加 OLED 屏幕配置
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    // 添加电池电量配置
    adc_oneshot_unit_handle_t adc1_handle_;
    adc_cali_handle_t adc1_cali_handle_;
    bool do_calibration_ = false;
    // 添加电源管理配置
    PowerSaveTimer* power_save_timer_;
    
    // 内存监控定时器
    esp_timer_handle_t memory_monitor_timer_ = nullptr;
    
    // 添加电池状态缓存
    int cached_battery_level_ = 0;
    bool cached_battery_charging_ = false;
    bool cached_battery_discharging_ = false;
    int64_t last_battery_read_time_ = 0;
    static const int64_t BATTERY_READ_INTERVAL_MS = 60000; // 60秒更新一次

    // 新增玩具触摸按键
    TouchButtonWrapper head_touch_button_;
    TouchButtonWrapper nose_touch_button_;
    TouchButtonWrapper belly_touch_button_;

    // 耳朵控制器
    EarController* ear_controller_ = nullptr;
    
    // 风扇控制器
    FanController* fan_controller_ = nullptr;
    
    // 触摸按钮到耳朵动作的映射方法
    void TriggerEarActionForTouch(const std::string& touch_type, bool is_long_press = false) {
        if (!ear_controller_) {
            ESP_LOGW(TAG, "No ear controller available for touch action");
            return;
        }
        
        ESP_LOGI(TAG, "Triggering ear action for touch: %s, long_press: %s", 
                 touch_type.c_str(), is_long_press ? "true" : "false");
        
        // 根据触摸类型和是否长按选择不同的耳朵动作
        if (touch_type == "head") {
            if (is_long_press) {
                // 长时间摸头 - 温和的开心动作
                ear_controller_->TriggerEmotion("happy");
            } else {
                // 摸头 - 好奇动作
                ear_controller_->TriggerEmotion("curious");
            }
        } else if (touch_type == "nose") {
            if (is_long_press) {
                // 长时间摸鼻子 - 兴奋动作
                ear_controller_->TriggerEmotion("excited");
            } else {
                // 摸鼻子 - 玩耍动作
                ear_controller_->TriggerEmotion("playful");
            }
        } else if (touch_type == "belly") {
            if (is_long_press) {
                // 长时间摸肚子 - 温和开心动作
                ear_controller_->TriggerEmotion("happy");
            } else {
                // 摸肚子 - 开心动作
                ear_controller_->TriggerEmotion("playful");
            }
        }
    }
    
    // 触摸频率跟踪和智能动作映射
    struct TouchHistory {
        std::string touch_type;
        int64_t timestamp;
        bool is_long_press;
    };
    
    std::vector<TouchHistory> touch_history_;
    static const int MAX_TOUCH_HISTORY = 10;
    static const int64_t TOUCH_FREQUENCY_WINDOW_MS = 3000; // 3秒窗口
    
    void TriggerSmartEarActionForTouch(const std::string& touch_type, bool is_long_press = false) {
        if (!ear_controller_) {
            ESP_LOGW(TAG, "No ear controller available for smart touch action");
            return;
        }
        
        int64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
        
        // 记录触摸历史
        TouchHistory new_touch = {touch_type, current_time, is_long_press};
        touch_history_.push_back(new_touch);
        
        // 保持历史记录在合理范围内
        if (touch_history_.size() > MAX_TOUCH_HISTORY) {
            touch_history_.erase(touch_history_.begin());
        }
        
        // 分析触摸模式
        int recent_touches = 0;
        int head_touches = 0;
        int nose_touches = 0;
        int belly_touches = 0;
        
        for (const auto& touch : touch_history_) {
            if (current_time - touch.timestamp < TOUCH_FREQUENCY_WINDOW_MS) {
                recent_touches++;
                if (touch.touch_type == "head") head_touches++;
                else if (touch.touch_type == "nose") nose_touches++;
                else if (touch.touch_type == "belly") belly_touches++;
            }
        }
        
        ESP_LOGI(TAG, "Touch analysis: recent=%d, head=%d, nose=%d, belly=%d", 
                 recent_touches, head_touches, nose_touches, belly_touches);
        
        // 根据触摸模式选择耳朵动作
        if (recent_touches >= 5) {
            // 高频触摸 - 兴奋动作
            ESP_LOGI(TAG, "High frequency touch detected, triggering excited action");
            ear_controller_->TriggerEmotion("excited");
        } else if (recent_touches >= 3) {
            // 中频触摸 - 玩耍动作
            ESP_LOGI(TAG, "Medium frequency touch detected, triggering playful action");
            ear_controller_->TriggerEmotion("playful");
        } else {
            // 低频触摸 - 根据具体位置选择动作
            if (touch_type == "head") {
                if (is_long_press) {
                    ear_controller_->TriggerEmotion("happy");
                } else {
                    ear_controller_->TriggerEmotion("curious");
                }
            } else if (touch_type == "nose") {
                if (is_long_press) {
                    ear_controller_->TriggerEmotion("excited");
                } else {
                    ear_controller_->TriggerEmotion("playful");
                }
            } else if (touch_type == "belly") {
                if (is_long_press) {
                    ear_controller_->TriggerEmotion("happy");
                } else {
                    ear_controller_->TriggerEmotion("playful");
                }
            }
        }
    }
    
    // // 触摸按键文本候选列表
    // std::vector<std::string> head_touch_texts_ = {
    //     "哈哈，摸摸你的头哦~",
    //     "哇，摸摸你的头，好舒服的头~",
    //     "我来摸摸你的小脑袋啦",
    //     "我在摸你头，头好痒痒的吧",
    //     "来给我摸摸头，摸摸头",
    //     "哇，摸摸你的头，你的头发软软的",
    //     "我要摸摸你的头发",
    //     "哇，摸摸你的头，头好温暖",
    //     "摸摸你的小脑瓜，小脑瓜好可爱",
    //     "摸摸你的头，头好舒服"
    // };
    
    // std::vector<std::string> nose_touch_texts_ = {
    //     "我来摸摸你的鼻子~",
    //     "我在摸摸你的鼻子，鼻子好痒痒~",
    //     "摸摸你的鼻子，好舒服",
    //     "摸摸你的小鼻子",
    //     "哇，你的鼻子好软软的",
    //     "我要摸摸你的鼻子，好舒服",
    //     "我在摸你的长长鼻子，它会喷水吗",
    //     "摸摸你的长鼻头",
    //     "你的鼻子好温暖",
    //     "摸摸你的鼻子，真开心",
    //     "鼻子好可爱，会喝水吗",
    //     "你的鼻子真可爱，会喝水吗",
    // };
    
    // std::vector<std::string> belly_touch_texts_ = {
    //     "我在摸摸你的肚子~",
    //     "我在摸摸你的肚子，肚子好痒痒",
    //     "摸摸你的小肚子，会跳舞吗",
    //     "在你肚子上跳舞，肚子好软软的",
    //     "摸摸你的小肚子，肚皮好痒痒",
    //     "摸摸你的小肚皮，肚皮好痒痒",
    //     "摸一下你的圆圆肚子好温暖",
    //     "摸摸肚子，真开心",
    //     "哇你的肚子好舒服，让我摸摸~"
    // };
    
    // std::vector<std::string> head_long_press_texts_ = {
    //     "长时间摸你的头~",
    //     "我在摸你的头摸了好久",
    //     "你的头被我摸得好舒服",
    //     "长时间摸摸头，继续摸摸~",
    //     "你的头被我摸得痒痒的",
    //     "摸头摸得停不下来，继续摸摸~",
    //     "你的头被我摸得好温暖",
    //     "长时间摸摸小脑袋，继续摸摸~",
    //     "你的头真可爱，被我摸了好久，好开心",
    //     "摸头摸得好久，继续摸摸~"
    // };
    
    // std::vector<std::string> nose_long_press_texts_ = {
    //     "我一直摸你的鼻子，你觉得好痒痒吗？要不要我再摸一会儿？",
    //     "我的手停留在你的鼻子上好久，你是不是很开心，想和我一起玩？",
    //     "我这样摸你的鼻子，你都快要打喷嚏啦，要不要和我一起做个鬼脸？",
    //     "你的鼻子被我摸得暖暖的，你觉得很舒服吗？你是不是也很喜欢我？",
    //     "我一直摸你的鼻子，你有点害羞了吗？要不要你也摸摸我？",
    //     "我的手好温暖，摸在你的鼻子上，我想和你多待一会儿！",
    //     "我摸你的鼻子这么久，是不是想和你说悄悄话呀？",
    //     "你的鼻子被我摸得软软的，我想和你一起做个游戏，好不好？",
    //     "我摸你的小鼻子这么久，你都快要笑出来啦，要不要一起笑一笑？",
    //     "谢谢你让我一直摸你的鼻子，我觉得我们是最好的朋友！"
    // };
    
    // std::vector<std::string> belly_long_press_texts_ = {
    //     "我一直摸你的肚子，你觉得好痒痒吗？要不要和我一起笑？",
    //     "我的手在你肚子上停留了好久，你觉得很温暖吗？好想和你抱一抱！",
    //     "你的肚子被我摸得软软的，我想和你一起跳舞，你愿意吗？",
    //     "我摸你的肚子这么久，你都快要咕咕叫啦，要不要我喂你点好吃的？",
    //     "我的手摸在你的肚皮上，你觉得很舒服吗？我们一起玩游戏吧！",
    //     "我一直摸你的肚子，你有点害羞了吗？要不要你也摸摸我？",
    //     "你的肚子被我摸得暖暖的，我想和你一起做个梦，好不好？",
    //     "我摸你的小肚子这么久，你都快要笑出声啦，要不要一起打个滚？",
    //     "谢谢你让我一直摸你的肚子，我觉得我们是最亲密的朋友！",
    //     "我的手好温柔，摸在你的肚子上，我想和你一直在一起！"
    // };

    std::vector<std::string> head_touch_texts_ = {
        "用户轻轻摸了摸我的小脑袋，我感觉很舒服，想和用户互动",
        "用户温柔地抚摸我的头部，让我感到被关爱，想表达感谢",
        "用户摸了我的头，让我很开心，想和用户一起玩耍",
        "用户轻抚我的脑袋，我感觉很温暖，想和用户亲近",
        "用户摸我的头，让我感到快乐，想和用户分享这份喜悦"
    };
    
    std::vector<std::string> head_long_press_texts_ = {
        "用户长时间抚摸我的小脑袋，让我感到非常舒适和安心，想和用户建立更深的情感连接",
        "用户持续摸我的头，让我感受到深深的爱意，想和用户一起度过美好时光",
        "用户长时间轻抚我的头部，让我感到被珍视，想和用户分享内心的温暖",
        "用户持续摸我的头，让我感到无比幸福，想和用户一起创造美好回忆",
        "用户长时间抚摸我的脑袋，让我感到被理解，想和用户建立深厚的友谊"
    };
    
    std::vector<std::string> nose_touch_texts_ = {
        "用户轻轻点了点我的小鼻子，让我感到很有趣，想和用户一起玩耍",
        "用户摸了我的鼻子，让我感到好奇，想和用户互动",
        "用户轻触我的鼻子，让我感到开心，想和用户一起笑",
        "用户摸我的鼻子，让我感到兴奋，想和用户一起探索",
        "用户点了我的鼻子，让我感到快乐，想和用户分享这份喜悦"
    };
    
    std::vector<std::string> nose_long_press_texts_ = {
        "用户一直摸我的鼻子，让我感到非常痒痒和有趣，想和用户一起做游戏",
        "用户持续摸我的鼻子，让我感到被关注，想和用户建立亲密关系",
        "用户长时间摸我的鼻子，让我感到很开心，想和用户一起创造快乐",
        "用户持续摸我的鼻子，让我感到被爱，想和用户分享内心的温暖",
        "用户长时间摸我的鼻子，让我感到无比幸福，想和用户一起度过美好时光"
    };
    
    std::vector<std::string> belly_touch_texts_ = {
        "用户轻轻摸了摸我的小肚子，让我感到很开心，想和用户一起玩耍",
        "用户摸了我的肚子，让我感到温暖，想和用户亲近",
        "用户轻抚我的肚子，让我感到舒适，想和用户一起放松",
        "用户摸我的肚子，让我感到快乐，想和用户分享这份喜悦",
        "用户摸了我的肚子，让我感到被关爱，想和用户建立情感连接"
    };
    
    std::vector<std::string> belly_long_press_texts_ = {
        "用户一直摸我的肚子，让我感到非常舒适和安心，想和用户一起享受这份温暖",
        "用户持续摸我的肚子，让我感到被珍视，想和用户建立深厚的友谊",
        "用户长时间摸我的肚子，让我感到无比幸福，想和用户一起创造美好回忆",
        "用户持续摸我的肚子，让我感到被理解，想和用户分享内心的温暖",
        "用户长时间摸我的肚子，让我感到被爱，想和用户一起度过美好时光"
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

    void InitializeMemoryMonitor() {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                // 内部内存统计
                size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                size_t min_free_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                
                // 外部内存统计 (SPIRAM)
                size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                size_t min_free_spiram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
                
                // 总内存统计
                size_t free_total = heap_caps_get_free_size(MALLOC_CAP_8BIT);
                size_t min_free_total = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
                
                ESP_LOGI(TAG, "--- 内存统计 ---");
                ESP_LOGI(TAG, "内部RAM: 当前空闲 %u 字节, 最小空闲 %u 字节", free_internal, min_free_internal);
                ESP_LOGI(TAG, "外部RAM: 当前空闲 %u 字节, 最小空闲 %u 字节", free_spiram, min_free_spiram);
                ESP_LOGI(TAG, "总计RAM: 当前空闲 %u 字节, 最小空闲 %u 字节", free_total, min_free_total);
                
                // 检查内存是否严重不足
                if (min_free_internal < 10000) {  // 如果最小空闲内存低于10KB
                    ESP_LOGW(TAG, "警告: 内部RAM严重不足!");
                }
            },
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "memory_monitor",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &memory_monitor_timer_));
        // 每10秒检查一次内存状态
        ESP_ERROR_CHECK(esp_timer_start_periodic(memory_monitor_timer_, 10 * 1000 * 1000));
        ESP_LOGI(TAG, "Memory monitor started");
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 180);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            
            auto codec = GetAudioCodec();
            codec->EnableInput(false);
            // 停止唤醒词检测
            // Application::GetInstance().StopWakeWordDetection();
        });
        power_save_timer_->OnExitSleepMode([this]() {
            auto codec = GetAudioCodec();
            codec->EnableInput(true);
            
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            // 重新启动唤醒词检测
            // Application::GetInstance().StartWakeWordDetection();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
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

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(codec_i2c_bus_, &io_config, &panel_io_));

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

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            {&font_puhui_14_1, &font_awesome_14_1});
    }

    void InitializeADC() {
        adc_oneshot_unit_init_cfg_t init_config1 = {
            .unit_id = ADC_UNIT_1
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle_));

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN,
            .bitwidth = ADC_WIDTH,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle_, VBAT_ADC_CHANNEL, &chan_config));

        adc_cali_handle_t handle = NULL;
        esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_WIDTH,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            do_calibration_ = true;
            adc1_cali_handle_ = handle;
            ESP_LOGI(TAG, "ADC Curve Fitting calibration succeeded");
        }
#endif // ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    }

    void InitializeTouchSensor() {
        ESP_LOGI(TAG, "Starting touch sensor initialization...");
        
        // 初始化触摸传感器 - 传入所有需要的通道
        uint32_t touch_channels[] = {TOUCH_CHANNEL_HEAD, TOUCH_CHANNEL_NOSE, TOUCH_CHANNEL_BELLY};
        int channel_count = sizeof(touch_channels) / sizeof(touch_channels[0]);
        
        ESP_LOGI(TAG, "Touch channels: HEAD=%d, NOSE=%d, BELLY=%d", 
                 TOUCH_CHANNEL_HEAD, TOUCH_CHANNEL_NOSE, TOUCH_CHANNEL_BELLY);
        
        TouchButtonWrapper::InitializeTouchSensor(touch_channels, channel_count);
        TouchButtonWrapper::StartTouchSensor();
        
        // 触摸传感器初始化完成后，创建所有按钮
        ESP_LOGI(TAG, "Creating touch buttons...");
        head_touch_button_.CreateButton();
        nose_touch_button_.CreateButton();
        belly_touch_button_.CreateButton();
        
        ESP_LOGI(TAG, "Touch sensor initialization completed successfully");
    }

    void InitializeEarController() {
        ESP_LOGI(TAG, "=== Starting ear controller initialization ===");
        ESP_LOGI(TAG, "GPIO pins: L_INA=%d, L_INB=%d, R_INA=%d, R_INB=%d",
                 LEFT_EAR_INA_GPIO, LEFT_EAR_INB_GPIO, RIGHT_EAR_INA_GPIO, RIGHT_EAR_INB_GPIO);
        
        // 创建TC118S耳朵控制器实例
        ESP_LOGI(TAG, "Creating Tc118sEarController instance");
        ear_controller_ = new Tc118sEarController(
            LEFT_EAR_INA_GPIO, LEFT_EAR_INB_GPIO,
            RIGHT_EAR_INA_GPIO, RIGHT_EAR_INB_GPIO
        );
        
        if (!ear_controller_) {
            ESP_LOGE(TAG, "Failed to create Tc118sEarController instance");
            return;
        }
        
        ESP_LOGI(TAG, "Tc118sEarController instance created successfully");
        
        // 初始化耳朵控制器
        ESP_LOGI(TAG, "Calling ear_controller_->Initialize()");
        esp_err_t ret = ear_controller_->Initialize();
        ESP_LOGI(TAG, "ear_controller_->Initialize() returned: %s", (ret == ESP_OK) ? "ESP_OK" : "ESP_FAIL");
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ear controller, switching to NoEarController");
            delete ear_controller_;
            
            ESP_LOGI(TAG, "Creating NoEarController instance as fallback");
            ear_controller_ = new NoEarController();
            
            if (!ear_controller_) {
                ESP_LOGE(TAG, "Failed to create NoEarController instance");
                return;
            }
            
            ESP_LOGI(TAG, "Calling NoEarController::Initialize()");
            ear_controller_->Initialize();
            ESP_LOGI(TAG, "NoEarController initialization completed");
        }
        
        ESP_LOGI(TAG, "=== Ear controller initialization completed successfully ===");
        // Note: Cannot use dynamic_cast due to -fno-rtti, but we can track the type during creation
        ESP_LOGI(TAG, "Final ear controller type: %s", 
                 (ear_controller_ != nullptr) ? "Valid controller" : "No controller");
    }

    void DelayedEarReset() {
        ESP_LOGI(TAG, "=== Starting delayed ear reset ===");
        
        // 创建异步任务来执行耳朵复位，避免阻塞主线程
        TaskHandle_t reset_task_handle = nullptr;
        BaseType_t task_created = xTaskCreate(
            [](void* param) {
                AstronautToysESP32S3* self = static_cast<AstronautToysESP32S3*>(param);
                if (self) {
                    self->ExecuteEarReset();
                }
                vTaskDelete(nullptr);
            },
            "ear_reset_task",
            4096,  // 堆栈大小
            this,  // 传递this指针
            5,     // 任务优先级
            &reset_task_handle
        );
        
        if (task_created == pdPASS) {
            ESP_LOGI(TAG, "Ear reset task created successfully");
        } else {
            ESP_LOGW(TAG, "Failed to create ear reset task, executing synchronously");
            // 如果任务创建失败，同步执行
            ExecuteEarReset();
        }
    }
    
    void ExecuteEarReset() {
        ESP_LOGI(TAG, "=== Executing ear reset ===");
        
        // 等待GPIO初始化完成
        vTaskDelay(pdMS_TO_TICKS(1000)); // 延迟1秒
        
        if (ear_controller_) {
            ESP_LOGI(TAG, "Ensuring ears are in default DOWN position after GPIO initialization");
            
            // 首先尝试复位到默认位置
            esp_err_t reset_ret = ear_controller_->ResetToDefault();
            if (reset_ret == ESP_OK) {
                ESP_LOGI(TAG, "Ears successfully reset to default DOWN position");
            } else {
                ESP_LOGW(TAG, "Failed to reset ears to default position");
            }
        } else {
            ESP_LOGW(TAG, "No ear controller available for delayed reset");
        }
        
        ESP_LOGI(TAG, "=== Ear reset execution completed ===");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            if (display_) {
                display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            if (display_) {
                 display_->ShowNotification(Lang::Strings::MAX_VOLUME);
            }
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            if (display_) {
                 display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            if (display_) {
                 display_->ShowNotification(Lang::Strings::MUTED);
            }
        });

        // 新增玩具触摸按键事件处理 - 使用新的事件接口
        head_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Head touch button clicked - Channel: %d", TOUCH_CHANNEL_HEAD);
            std::string touch_text = GetRandomText(head_touch_texts_);
            std::string action_text = "抚摸头部：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("head", false);
            // 使用新的事件接口，发送带有动作描述的文本
            Application::GetInstance().PostTouchEvent(action_text);
        });
        
        head_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Head touch button long pressed - Channel: %d", TOUCH_CHANNEL_HEAD);
            std::string touch_text = GetRandomText(head_long_press_texts_);
            std::string action_text = "长时间抚摸头部：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("head", true);
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(action_text);
        });
        
        nose_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Nose touch button clicked - Channel: %d", TOUCH_CHANNEL_NOSE);
            std::string touch_text = GetRandomText(nose_touch_texts_);
            std::string action_text = "抚摸鼻子：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("nose", false);
            // 使用新的事件接口，发送带有动作描述的文本
            Application::GetInstance().PostTouchEvent(action_text);
        });
        
        nose_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Nose touch button long pressed - Channel: %d", TOUCH_CHANNEL_NOSE);
            std::string touch_text = GetRandomText(nose_long_press_texts_);
            std::string action_text = "长时间抚摸鼻子：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("nose", true);
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(action_text);
        });
        
        belly_touch_button_.OnClick([this]() {
            ESP_LOGI(TAG, "Belly touch button clicked - Channel: %d", TOUCH_CHANNEL_BELLY);
            std::string touch_text = GetRandomText(belly_touch_texts_);
            std::string action_text = "抚摸肚子：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("belly", false);
            // 使用新的事件接口，发送带有动作描述的文本
            Application::GetInstance().PostTouchEvent(action_text);
        });
        
        belly_touch_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "Belly touch button long pressed - Channel: %d", TOUCH_CHANNEL_BELLY);
            std::string touch_text = GetRandomText(belly_long_press_texts_);
            std::string action_text = "长时间抚摸肚子：" + touch_text;
            
            if (display_) {
                display_->ShowNotification(action_text);
            }
            // 触发耳朵动作
            TriggerSmartEarActionForTouch("belly", true);
            // 使用新的事件接口
            Application::GetInstance().PostTouchEvent(action_text);
        });

        // // KEY1 按钮
        // key1_button_.OnClick([]() {
        //     auto& app = Application::GetInstance();
        //     app.ToggleChatState();  // 切换聊天状态
        // });
        // key1_button_.OnLongPress([]() {
        //     auto& app = Application::GetInstance();
        //     // 长按处理，比如进入配置模式
        //     ESP_LOGI(TAG, "KEY1 Long Pressed - Placeholder action");
        // });

        // // KEY2 按钮
        // key1_button_.OnClick([]() {
        //     auto& app = Application::GetInstance();
        //     // 处理 KEY2 点击事件
        //     ESP_LOGI(TAG, "KEY2 Clicked - Placeholder action");
        // });
        // key1_button_.OnDoubleClick([]() {
        //     auto& app = Application::GetInstance();
        //     // 处理 KEY2 双击事件
        //     ESP_LOGI(TAG, "KEY2 Double Clicked - Placeholder action");
        // });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        
        // 初始化风扇控制器
        fan_controller_ = new FanController(FAN_BUTTON_GPIO, FAN_GPIO, LEDC_CHANNEL_0);
        ESP_LOGI(TAG, "Fan controller initialized in board");
        
        ESP_LOGI(TAG, "IoT devices initialized with MCP protocol");
    }

public:
    AstronautToysESP32S3() : 
    boot_button_(BOOT_BUTTON_GPIO),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
    key1_button_(KEY1_BUTTON_GPIO),
    key2_button_(KEY2_BUTTON_GPIO),
    head_touch_button_(TOUCH_CHANNEL_HEAD, 0.15f),    // 触摸按钮对象创建
    nose_touch_button_(TOUCH_CHANNEL_NOSE, 0.15f),    // 触摸按钮对象创建
    belly_touch_button_(TOUCH_CHANNEL_BELLY, 0.15f) { // 触摸按钮对象创建
        
        InitializeADC();
        InitializeCodecI2c();
        InitializeSsd1306Display();
        InitializeTouchSensor();  // 先初始化触摸传感器
        InitializeButtons();      // 再初始化按钮事件
        InitializePowerSaveTimer();
        InitializeEarController(); // 初始化耳朵控制器
        // InitializeMemoryMonitor();  // 初始化内存监控
        InitializeTools();
        
        // 延迟执行耳朵复位，确保GPIO初始化完成
        ESP_LOGI(TAG, "Scheduling delayed ear reset");
        DelayedEarReset();
    }

    ~AstronautToysESP32S3() {
        if (memory_monitor_timer_) {
            esp_timer_stop(memory_monitor_timer_);
            esp_timer_delete(memory_monitor_timer_);
        }
        
        // 清理耳朵控制器内存
        if (ear_controller_) {
            ear_controller_->Deinitialize();
            delete ear_controller_;
            ear_controller_ = nullptr;
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led_strip(BUILTIN_LED_GPIO);
        return &led_strip;
    }


    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0,
           AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
           AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
           AUDIO_CODEC_ES8311_ADDR, false);
       return &audio_codec;
   }
    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual EarController* GetEarController() override {
        ESP_LOGI(TAG, "GetEarController called, returning: %s", ear_controller_ ? "valid" : "null");
        return ear_controller_;
    }

    virtual FanController* GetFanController() override {
        ESP_LOGI(TAG, "GetFanController called, returning: %s", fan_controller_ ? "valid" : "null");
        return fan_controller_;
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) {
        int64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
        
        // 仅当不是首次调用且距离上次读取不到30秒，才返回缓存值
        if (last_battery_read_time_ > 0 && current_time - last_battery_read_time_ < BATTERY_READ_INTERVAL_MS) {
            level = cached_battery_level_;
            charging = cached_battery_charging_;
            discharging = cached_battery_discharging_;
            return true;
        }
        
        if (!adc1_handle_) {
            InitializeADC();
        }

        int raw_value = 0;
        int voltage = 0;

        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle_, VBAT_ADC_CHANNEL, &raw_value));

        if (do_calibration_) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle_, raw_value, &voltage));
            voltage = voltage * 3 / 2; // compensate for voltage divider
            ESP_LOGI(TAG, "Calibrated voltage: %d mV", voltage);
        } else {
            ESP_LOGI(TAG, "Raw ADC value: %d", raw_value);
            voltage = raw_value;
        }

        voltage = voltage < EMPTY_BATTERY_VOLTAGE ? EMPTY_BATTERY_VOLTAGE : voltage;
        voltage = voltage > FULL_BATTERY_VOLTAGE ? FULL_BATTERY_VOLTAGE : voltage;

        // 计算电量百分比
        level = (voltage - EMPTY_BATTERY_VOLTAGE) * 100 / (FULL_BATTERY_VOLTAGE - EMPTY_BATTERY_VOLTAGE);

        // charging = false;
        ESP_LOGI(TAG, "Battery Level: %d%%, Charging: %s", level, charging ? "Yes" : "No");
        
        // 更新缓存和时间戳
        cached_battery_level_ = level;
        cached_battery_charging_ = charging;
        cached_battery_discharging_ = discharging;
        last_battery_read_time_ = current_time;
        
        return true;
    }

};

DECLARE_BOARD(AstronautToysESP32S3);
