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
#include <driver/i2c.h>
#include <driver/spi_common.h>

#define TAG "esp_sparkbot"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
private:
    bool output_enabled_ = false;

    // 添加一个新的辅助函数来打印ES8311关键寄存器状态
    void PrintCodecStatus() {
        uint8_t reg_values[8];
        
        // 读取关键寄存器状态
        ReadReg(ES8311_RESET_REG00, &reg_values[0]);
        ReadReg(ES8311_CLK_MANAGER_REG01, &reg_values[1]);
        ReadReg(ES8311_SYSTEM_REG08, &reg_values[2]);
        ReadReg(ES8311_SYSTEM_REG09, &reg_values[3]);
        ReadReg(ES8311_ADC_REG10, &reg_values[4]);
        ReadReg(ES8311_DAC_REG31, &reg_values[5]);
        ReadReg(ES8311_DAC_REG37, &reg_values[6]);
        ReadReg(ES8311_GPIO_REG44, &reg_values[7]);
        
        ESP_LOGI(TAG, "ES8311 寄存器状态:");
        ESP_LOGI(TAG, "RESET_REG00: 0x%02X (期望值: 0x00)", reg_values[0]);
        ESP_LOGI(TAG, "CLK_MANAGER_REG01: 0x%02X (期望值: 0x3F)", reg_values[1]);
        ESP_LOGI(TAG, "SYSTEM_REG08: 0x%02X (工作模式)", reg_values[2]);
        ESP_LOGI(TAG, "SYSTEM_REG09: 0x%02X (静音控制)", reg_values[3]);
        ESP_LOGI(TAG, "ADC_REG10: 0x%02X (ADC电源)", reg_values[4]);
        ESP_LOGI(TAG, "DAC_REG31: 0x%02X (DAC电源)", reg_values[5]);
        ESP_LOGI(TAG, "DAC_REG37: 0x%02X (DAC控制)", reg_values[6]);
        ESP_LOGI(TAG, "GPIO_REG44: 0x%02X (GPIO配置)", reg_values[7]);
    }

    bool PerformReset() {
        ESP_LOGI(TAG, "执行ES8311电源复位流程");
        
        const gpio_num_t vcc_pin = static_cast<gpio_num_t>(AUDIO_CODEC_VCC_CTL);
        
        // 检查当前电源状态
        int power_level = gpio_get_level(vcc_pin);
        ESP_LOGI(TAG, "ES8311当前电源状态: %d", power_level);
        
        // 关闭电源
        gpio_set_level(vcc_pin, 0);
        ESP_LOGI(TAG, "ES8311电源关闭");
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 打开电源
        gpio_set_level(vcc_pin, 1);
        ESP_LOGI(TAG, "ES8311电源打开");
        vTaskDelay(pdMS_TO_TICKS(300));
        
        // 检查I2C通信是否正常
        uint8_t chip_id;
        if (!ReadReg(ES8311_CHIP_ID_REG00, &chip_id)) {
            ESP_LOGE(TAG, "无法读取ES8311芯片ID，I2C通信可能有问题");
            return false;
        }
        ESP_LOGI(TAG, "ES8311芯片ID: 0x%02X (期望值: 0xE0)", chip_id);
        
        // 打印复位前的寄存器状态
        ESP_LOGI(TAG, "复位前的寄存器状态:");
        PrintCodecStatus();
        
        // 执行软复位
        WriteReg(ES8311_RESET_REG00, 0x1F);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(ES8311_RESET_REG00, 0x00);
        vTaskDelay(pdMS_TO_TICKS(20));
        
        // 打印复位后的寄存器状态
        ESP_LOGI(TAG, "复位后的寄存器状态:");
        PrintCodecStatus();
        
        ESP_LOGI(TAG, "ES8311复位完成");
        return true;
    }

public:
    SparkBotEs8311AudioCodec(i2c_master_bus_handle_t i2c_bus_handle)
        : Es8311AudioCodec(
              i2c_bus_handle,
              AUDIO_CODEC_ES8311_ADDR,     // I2C地址
              AUDIO_INPUT_SAMPLE_RATE,     // 输入采样率
              AUDIO_OUTPUT_SAMPLE_RATE,    // 输出采样率
              AUDIO_I2S_GPIO_MCLK,         // MCLK
              AUDIO_I2S_GPIO_BCLK,         // BCLK
              AUDIO_I2S_GPIO_WS,           // WS  
              AUDIO_I2S_GPIO_DOUT,         // DOUT
              AUDIO_I2S_GPIO_DIN,          // DIN
              AUDIO_CODEC_PA_PIN,          // 功放控制引脚
              AUDIO_I2S_GPIO_MCLK != GPIO_NUM_NC  // 是否使用MCLK
          ) {
        
        ESP_LOGI(TAG, "SparkBotEs8311AudioCodec创建，执行额外的初始化");
        
        // 先复位一次确保状态正确
        if (!PerformReset()) {
            ESP_LOGE(TAG, "ES8311复位失败");
            return;
        }
        
        // 初始化完成后打印最终状态
        ESP_LOGI(TAG, "初始化完成后的最终状态:");
        PrintCodecStatus();
        
        ESP_LOGI(TAG, "SparkBotEs8311AudioCodec初始化完成");
    }

    void EnableOutput(bool enable) override {
        ESP_LOGI(TAG, "正在%s输出...", enable ? "启用" : "禁用");
        
        if (enable == output_enabled_) {
            ESP_LOGI(TAG, "输出已经是%s状态，无需改变", enable ? "启用" : "禁用");
            return;
        }
        
        // 打印切换前的状态
        ESP_LOGI(TAG, "切换前的状态:");
        PrintCodecStatus();
        
        output_enabled_ = enable;
        
        if (enable) {
            Es8311AudioCodec::EnableOutput(enable);
            ESP_LOGI(TAG, "SparkBot ES8311输出已启用");
        } else {
            Es8311AudioCodec::EnableOutput(enable);
            ESP_LOGI(TAG, "SparkBot ES8311输出已禁用");
        }
        
        // 打印切换后的状态
        ESP_LOGI(TAG, "切换后的状态:");
        PrintCodecStatus();
    }
};

class EspSparkBot : public WifiBoard {
private:
    Button boot_button_;
    Display* display_;
    i2c_master_bus_handle_t i2c_bus_handle_;

    // 初始化ES8311编解码器电源
    void InitializeCodecPower() {
        const gpio_num_t codec_vcc_pin = static_cast<gpio_num_t>(AUDIO_CODEC_VCC_CTL);
        
        // 配置电源控制引脚为输出
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << codec_vcc_pin);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        
        // 初始化为关闭状态
        gpio_set_level(codec_vcc_pin, 0);
        ESP_LOGI(TAG, "ES8311电源初始化为关闭状态");
        
        // 打开电源
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(codec_vcc_pin, 1);
        ESP_LOGI(TAG, "ES8311电源已启用");
        
        // 等待电源稳定
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_LOGI(TAG, "ES8311电源已稳定");
    }

    void InitializeI2c() {
        // 配置I2C - 使用新版ESP-IDF I2C主机总线API
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = true,
            },
        };
        // 创建I2C主总线
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_handle_));
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
        // 先初始化ES8311电源
        InitializeCodecPower();
        
        // 然后初始化其他硬件
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
         static SparkBotEs8311AudioCodec audio_codec(i2c_bus_handle_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(EspSparkBot);
