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

#define TAG "esp_sparkbot"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

// 前置声明
class EspSparkBot;

// 首先定义EspSparkBot类
class EspSparkBot : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Display* display_;
    
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
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
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
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
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
        thing_manager.AddThing(iot::CreateThing("Chassis"));
    }

public:
    EspSparkBot() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeAudioPower(); // 在其他初始化前先初始化音频电源
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override;  // 前置声明，实现在后面

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
};

// 获取正确类型的板子实例的辅助函数
EspSparkBot* GetEspSparkBot() {
    // Board::GetInstance()返回的是基类引用
    Board& board = Board::GetInstance();
    
    // 在这个系统中我们知道GetInstance返回的是EspSparkBot
    // 由于-fno-rtti编译选项，不能使用dynamic_cast，改用static_cast
    EspSparkBot* esp_board = static_cast<EspSparkBot*>(&board);
    
    return esp_board;
}

// 现在定义SparkBotEs8311AudioCodec类，它可以正确引用EspSparkBot
class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
private:
    bool initialized_as_slave_ = false;

public:
    SparkBotEs8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                      gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                      gpio_num_t pa_pin, uint8_t es8311_addr)
        : Es8311AudioCodec(i2c_master_handle, i2c_port, input_sample_rate, output_sample_rate,
                           mclk, bclk, ws, dout, din, pa_pin, es8311_addr, false) { // 明确指定不使用MCLK
    }

    void EnableInput(bool enable) override {
        if (enable == input_enabled_) {
            return;
        }
        
        if (enable) {
            // 使用工厂函数获取EspSparkBot实例
            EspSparkBot* board = GetEspSparkBot();
            if (board) {
                // 确保音频电路已上电
                board->SetAudioPower(true);
            }
            
            // 让父类方法处理实际的I2S和ES8311配置
            Es8311AudioCodec::EnableInput(true);
        } else {
            Es8311AudioCodec::EnableInput(false);
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!output_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_) {
                    EspSparkBot* board = GetEspSparkBot();
                    if (board) {
                        board->SetAudioPower(false);
                    }
                }
            }
        }
    }

    void EnableOutput(bool enable) override {
        if (enable == output_enabled_) {
            return;
        }
        
        if (enable) {
            // 使用工厂函数获取EspSparkBot实例
            EspSparkBot* board = GetEspSparkBot();
            if (board) {
                // 确保音频电路已上电
                board->SetAudioPower(true);
            }
            
            // 让父类方法处理实际的I2S和ES8311配置
            Es8311AudioCodec::EnableOutput(true);
        } else {
            // Display IO和PA IO冲突，特殊处理
            if (pa_pin_ != GPIO_NUM_NC) {
                gpio_set_level(pa_pin_, 0);
            }
            ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
            output_enabled_ = false;
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!input_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_) {
                    EspSparkBot* board = GetEspSparkBot();
                    if (board) {
                        board->SetAudioPower(false);
                    }
                }
            }
        }
    }
};

// 现在实现之前前置声明的EspSparkBot的GetAudioCodec方法
AudioCodec* EspSparkBot::GetAudioCodec() {
    static SparkBotEs8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
        AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
    return &audio_codec;
}

DECLARE_BOARD(EspSparkBot);
