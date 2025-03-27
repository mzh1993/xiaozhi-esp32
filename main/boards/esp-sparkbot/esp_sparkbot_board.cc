#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "font_awesome_symbols.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <cstring>

#define TAG "esp_sparkbot"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

// 全局指针，用于在回调中安全访问EspSparkBot实例
static void* g_esp_sparkbot_instance = nullptr;

// 前置声明
class EspSparkBot;

// 音频电源控制的回调函数类型
typedef void (*AudioPowerControlCallback)(bool enable);

// 音频电源控制的全局回调函数
void GlobalAudioPowerControl(bool enable);

// 首先完整定义SparkBotEs8311AudioCodec类
class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
private:
    bool initialization_failed_ = false;
    AudioPowerControlCallback power_control_cb_ = nullptr;

public:
    // 显式暴露状态标志位，以便外部检查
    bool IsInitializationFailed() const { return initialization_failed_; }

    // 构造函数直接调用父类构造函数
    SparkBotEs8311AudioCodec(void* i2c_dev, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                      gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                      gpio_num_t pa_pin, uint8_t es8311_addr, AudioPowerControlCallback power_cb)
        : Es8311AudioCodec(i2c_dev, i2c_port, input_sample_rate, output_sample_rate, 
                          mclk, bclk, ws, dout, din, pa_pin, es8311_addr, false), // use_mclk=false
          power_control_cb_(power_cb) {
        
        ESP_LOGI(TAG, "Creating SparkBotEs8311AudioCodec instance");
        // 不做任何复杂初始化，所有初始化工作已在基类构造函数中完成
        
        // 验证回调函数
        if (power_control_cb_ == nullptr) {
            ESP_LOGW(TAG, "No power control callback provided!");
        }
    }

    void EnableInput(bool enable) override {
        if (enable == input_enabled_) {
            return;
        }
        
        if (enable) {
            // 使用回调函数启用音频电源
            if (power_control_cb_) {
                power_control_cb_(true);
            }
            
            // 让父类方法处理实际的I2S和ES8311配置
            Es8311AudioCodec::EnableInput(true);
        } else {
            Es8311AudioCodec::EnableInput(false);
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!output_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_ && power_control_cb_) {
                    power_control_cb_(false);
                }
            }
        }
    }

    void EnableOutput(bool enable) override {
        if (enable == output_enabled_) {
            return;
        }
        
        if (enable) {
            // 使用回调函数启用音频电源
            if (power_control_cb_) {
                power_control_cb_(true);
            }
            
            // 让父类方法处理实际的I2S和ES8311配置
            Es8311AudioCodec::EnableOutput(true);
        } else {
            // Display IO和PA IO冲突，特殊处理
            if (pa_pin_ != GPIO_NUM_NC) {
                gpio_set_level(pa_pin_, 0);
            }
            
            if (output_dev_) {
                ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
            }
            output_enabled_ = false;
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!input_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_ && power_control_cb_) {
                    power_control_cb_(false);
                }
            }
        }
    }

    ~SparkBotEs8311AudioCodec() {
        ESP_LOGI(TAG, "Destroying SparkBotEs8311AudioCodec...");
        // 基类析构函数会处理资源释放
    }
};

// 现在定义EspSparkBot类
class EspSparkBot : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    Button boot_button_;
    Display* display_;
    SparkBotEs8311AudioCodec* audio_codec_ptr_ = nullptr;
    
    // 添加音频电源控制初始化函数
    void InitializeAudioPower() {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << AUDIO_PREP_VCC_CTL),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // 默认启用音频电路供电
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_PREP_VCC_CTL, 1));
        ESP_LOGI(TAG, "Audio power enabled");
        
        // 给电路足够的启动时间
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_mst_config;
        memset(&i2c_mst_config, 0, sizeof(i2c_master_bus_config_t));
        i2c_mst_config.i2c_port = I2C_NUM_0;
        i2c_mst_config.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        i2c_mst_config.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
        i2c_mst_config.glitch_ignore_cnt = 7;
        i2c_mst_config.flags.enable_internal_pullup = true;
        
        ESP_LOGI(TAG, "Creating I2C master bus with config:");
        ESP_LOGI(TAG, "  Port: %d, SCL: %d, SDA: %d", 
                i2c_mst_config.i2c_port, 
                i2c_mst_config.scl_io_num, 
                i2c_mst_config.sda_io_num);
        
        esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_);
        ESP_ERROR_CHECK(ret);
        ESP_LOGI(TAG, "I2C master bus created: %p", (void*)i2c_bus_);
        
        // 创建I2C设备
        i2c_device_config_t dev_cfg;
        memset(&dev_cfg, 0, sizeof(i2c_device_config_t));
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = AUDIO_CODEC_ES8311_ADDR;
        dev_cfg.scl_speed_hz = 100000;
        
        ESP_LOGI(TAG, "Adding I2C device with config:");
        ESP_LOGI(TAG, "  Address: 0x%02x, Speed: %lu Hz", 
                dev_cfg.device_address, 
                dev_cfg.scl_speed_hz);
        
        ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
        ESP_ERROR_CHECK(ret);
        ESP_LOGI(TAG, "I2C device added: %p", (void*)i2c_dev_);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_GPIO;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_GPIO;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_GPIO;
        io_config.dc_gpio_num = DISPLAY_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
        // thing_manager.AddThing(iot::CreateThing("Chassis"));
    }

    bool TestEsCodecI2c() {
        ESP_LOGI(TAG, "Testing I2C communication with ES8311...");
        uint8_t reg_addr = 0xFD; // ES8311 Chip ID1 register
        uint8_t reg_data = 0;
        esp_err_t ret;
        
        // 显示使用的I2C地址
        ESP_LOGI(TAG, "ES8311 I2C address: 0x%02x", AUDIO_CODEC_ES8311_ADDR);
        
        // 使用正确的I2C函数和设备句柄
        ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C write failed: %s (0x%x)", esp_err_to_name(ret), ret);
            
            // 尝试重新创建设备，可能使用不同的地址
            i2c_master_bus_rm_device(i2c_dev_);
            
            // 尝试不同的I2C地址（如果CE引脚被拉高，地址可能是0x19）
            uint8_t alt_addr = 0x19;
            ESP_LOGI(TAG, "Trying alternative I2C address: 0x%02x", alt_addr);
            
            i2c_device_config_t alt_dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = alt_addr,
                .scl_speed_hz = 100000,
            };
            ret = i2c_master_bus_add_device(i2c_bus_, &alt_dev_cfg, &i2c_dev_);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create I2C device with alternative address: %s (0x%x)", 
                        esp_err_to_name(ret), ret);
                return false;
            }
            
            ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "I2C write with alternative address also failed: %s (0x%x)", 
                        esp_err_to_name(ret), ret);
                return false;
            }
            ESP_LOGI(TAG, "I2C write to alternative address successful!");
        }
        
        // 再读取寄存器
        ret = i2c_master_receive(i2c_dev_, &reg_data, 1, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C read failed: %s (0x%x)", esp_err_to_name(ret), ret);
            return false;
        }
        
        ESP_LOGI(TAG, "ES8311 chip ID read: 0x%02x (expected 0x83)", reg_data);
        
        // 尝试读取另一个寄存器：版本ID (0xFE)
        reg_addr = 0xFE;
        ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
        if (ret == ESP_OK) {
            ret = i2c_master_receive(i2c_dev_, &reg_data, 1, -1);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "ES8311 version ID read: 0x%02x", reg_data);
            }
        }
        
        // 尝试读取芯片ID2 (0xFF)
        reg_addr = 0xFF;
        ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
        if (ret == ESP_OK) {
            ret = i2c_master_receive(i2c_dev_, &reg_data, 1, -1);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "ES8311 chip ID2 read: 0x%02x", reg_data);
            }
        }
        
        return true; // 如果能够读取芯片ID，则通信正常
    }

public:
    EspSparkBot() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Initializing EspSparkBot...");
        
        // 存储实例指针以便全局访问
        g_esp_sparkbot_instance = this;
        
        // 初始化音频电源
        InitializeAudioPower();
        ESP_LOGI(TAG, "Audio power initialized");
        
        // 初始化I2C总线
        ESP_LOGI(TAG, "Initializing I2C bus on SDA:%d, SCL:%d", AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        InitializeI2c();
        
        // 测试ES8311 I2C通信
        bool i2c_test_result = TestEsCodecI2c();
        if (!i2c_test_result) {
            ESP_LOGE(TAG, "ES8311 I2C communication test failed! Check connections and power");
            // 这里不要直接返回，让其他部分继续初始化
        } else {
            ESP_LOGI(TAG, "ES8311 I2C communication test passed!");
        }
        
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        if (audio_codec_ptr_ == nullptr) {
            ESP_LOGI(TAG, "Creating new ES8311 audio codec instance...");
            
            // 测试I2C通信，确保ES8311可访问
            if (!TestEsCodecI2c()) {
                ESP_LOGE(TAG, "ES8311 I2C test failed before codec creation!");
                
                // 尝试重置音频电源
                ESP_LOGI(TAG, "Trying to reset audio power...");
                SetAudioPower(false);
                vTaskDelay(pdMS_TO_TICKS(100));
                SetAudioPower(true);
                vTaskDelay(pdMS_TO_TICKS(200));
                
                // 再次测试
                if (!TestEsCodecI2c()) {
                    ESP_LOGE(TAG, "ES8311 I2C test still failed after power reset!");
                    return nullptr;
                }
            }
            
            try {
                // 打印关键参数
                ESP_LOGI(TAG, "Before creating codec - I2C device handle: %p", (void*)i2c_dev_);
                ESP_LOGI(TAG, "GlobalAudioPowerControl address: %p", (void*)GlobalAudioPowerControl);
                
                // 验证i2c_dev_有效性
                if (i2c_dev_ == nullptr) {
                    ESP_LOGE(TAG, "I2C device handle is NULL! Re-initializing I2C...");
                    
                    // 尝试重新初始化I2C
                    InitializeI2c();
                    
                    if (i2c_dev_ == nullptr) {
                        ESP_LOGE(TAG, "Failed to re-initialize I2C device handle");
                        return nullptr;
                    }
                }
                
                // 创建编解码器实例
                audio_codec_ptr_ = new SparkBotEs8311AudioCodec(
                    (void*)i2c_dev_, I2C_NUM_0, 
                    AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                    AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, 
                    AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                    AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, 
                    GlobalAudioPowerControl);
                
                if (audio_codec_ptr_ == nullptr) {
                    ESP_LOGE(TAG, "Failed to allocate memory for audio codec!");
                    return nullptr;
                }
                
                // 检查初始化是否失败
                SparkBotEs8311AudioCodec* codec = static_cast<SparkBotEs8311AudioCodec*>(audio_codec_ptr_);
                if (codec->IsInitializationFailed()) {
                    ESP_LOGE(TAG, "Audio codec initialization failed!");
                    delete audio_codec_ptr_;
                    audio_codec_ptr_ = nullptr;
                    return nullptr;
                }
                
                ESP_LOGI(TAG, "ES8311 audio codec created successfully");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception during ES8311 audio codec creation: %s", e.what());
                if (audio_codec_ptr_ != nullptr) {
                    delete audio_codec_ptr_;
                    audio_codec_ptr_ = nullptr;
                }
                return nullptr;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception during ES8311 audio codec creation");
                if (audio_codec_ptr_ != nullptr) {
                    delete audio_codec_ptr_;
                    audio_codec_ptr_ = nullptr;
                }
                return nullptr;
            }
        } else {
            ESP_LOGI(TAG, "Using existing audio codec: %p", audio_codec_ptr_);
        }
        
        return audio_codec_ptr_;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    // 添加电源控制方法
    void SetAudioPower(bool enable) {
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_PREP_VCC_CTL, enable ? 1 : 0));
        ESP_LOGI(TAG, "Audio power %s", enable ? "enabled" : "disabled");
        
        if (enable) {
            // 给电路足够的上电稳定时间
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // 重新初始化ES8311音频编解码器
    bool ReinitializeAudioCodec() {
        ESP_LOGI(TAG, "Reinitializing ES8311 audio codec...");
        
        // 首先关闭音频电源
        SetAudioPower(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // 重新上电
        SetAudioPower(true);
        vTaskDelay(pdMS_TO_TICKS(300));  // 给足够的上电时间
        
        // 测试I2C通信
        if (!TestEsCodecI2c()) {
            ESP_LOGE(TAG, "Failed to communicate with ES8311 after power cycle!");
            return false;
        }
        
        // 如果有现有实例，释放资源
        if (audio_codec_ptr_ != nullptr) {
            ESP_LOGI(TAG, "Deleting existing audio codec instance");
            delete audio_codec_ptr_;  // 析构函数会处理所有资源释放
            audio_codec_ptr_ = nullptr;
        }
        
        // 重新创建音频编解码器实例
        ESP_LOGI(TAG, "Creating new ES8311 audio codec instance...");
        try {
            // 打印关键参数
            ESP_LOGI(TAG, "In ReinitializeAudioCodec - I2C device handle: %p", (void*)i2c_dev_);
            ESP_LOGI(TAG, "GlobalAudioPowerControl address: %p", (void*)GlobalAudioPowerControl);
            
            // 验证i2c_dev_有效性
            if (i2c_dev_ == nullptr) {
                ESP_LOGE(TAG, "I2C device handle is NULL! Re-initializing I2C...");
                
                // 尝试重新初始化I2C
                InitializeI2c();
                
                if (i2c_dev_ == nullptr) {
                    ESP_LOGE(TAG, "Failed to re-initialize I2C device handle");
                    return false;
                }
            }
            
            // 创建新的编解码器实例
            audio_codec_ptr_ = new SparkBotEs8311AudioCodec(
                (void*)i2c_dev_, I2C_NUM_0, 
                AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, 
                AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR,
                GlobalAudioPowerControl);
                
            if (audio_codec_ptr_ == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for audio codec!");
                return false;
            }
            
            // 检查是否初始化失败
            SparkBotEs8311AudioCodec* codec = static_cast<SparkBotEs8311AudioCodec*>(audio_codec_ptr_);
            if (codec->IsInitializationFailed()) {
                ESP_LOGE(TAG, "Audio codec initialization failed!");
                delete audio_codec_ptr_;
                audio_codec_ptr_ = nullptr;
                return false;
            }
            
            ESP_LOGI(TAG, "ES8311 audio codec reinitialized successfully");
            return true;
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception during ES8311 audio codec creation: %s", e.what());
            if (audio_codec_ptr_ != nullptr) {
                delete audio_codec_ptr_;
                audio_codec_ptr_ = nullptr;
            }
            return false;
        } catch (...) {
            ESP_LOGE(TAG, "Unknown exception during ES8311 audio codec creation");
            if (audio_codec_ptr_ != nullptr) {
                delete audio_codec_ptr_;
                audio_codec_ptr_ = nullptr;
            }
            return false;
        }
    }
};

// 获取正确类型的板子实例的辅助函数
EspSparkBot* GetEspSparkBot() {
    // 优先尝试使用全局指针
    if (g_esp_sparkbot_instance != nullptr) {
        return static_cast<EspSparkBot*>(g_esp_sparkbot_instance);
    }
    
    // 回退方法：从Board中获取
    Board& board = Board::GetInstance();
    
    // 在这个系统中我们知道GetInstance返回的是EspSparkBot
    // 由于-fno-rtti编译选项，不能使用dynamic_cast，改用static_cast
    EspSparkBot* esp_board = static_cast<EspSparkBot*>(&board);
    
    return esp_board;
}

// 全局音频电源控制回调实现
void GlobalAudioPowerControl(bool enable) {
    ESP_LOGI(TAG, "GlobalAudioPowerControl called with enable=%d", enable);
    ESP_LOGI(TAG, "Global board instance: %p", g_esp_sparkbot_instance);
    
    // 使用try-catch捕获任何可能的异常
    try {
        EspSparkBot* board = nullptr;
        
        // 首先尝试使用全局指针
        if (g_esp_sparkbot_instance != nullptr) {
            board = static_cast<EspSparkBot*>(g_esp_sparkbot_instance);
            ESP_LOGI(TAG, "Retrieved board instance from global pointer: %p", board);
        } else {
            // 回退方法：从Board中获取
            ESP_LOGI(TAG, "Global pointer is NULL, trying to get instance from Board::GetInstance()");
            try {
                Board& board_ref = Board::GetInstance();
                board = static_cast<EspSparkBot*>(&board_ref);
                ESP_LOGI(TAG, "Retrieved board instance from Board::GetInstance(): %p", board);
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception during Board::GetInstance(): %s", e.what());
                return;
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception during Board::GetInstance()");
                return;
            }
        }
        
        if (board) {
            ESP_LOGI(TAG, "Setting audio power: %d", enable);
            // 使用try-catch以防SetAudioPower方法抛出异常
            try {
                board->SetAudioPower(enable);
                ESP_LOGI(TAG, "Audio power set successful");
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "Exception during SetAudioPower: %s", e.what());
            } catch (...) {
                ESP_LOGE(TAG, "Unknown exception during SetAudioPower");
            }
        } else {
            ESP_LOGE(TAG, "Failed to get EspSparkBot instance for power control!");
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in GlobalAudioPowerControl: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in GlobalAudioPowerControl");
    }
}

DECLARE_BOARD(EspSparkBot);
