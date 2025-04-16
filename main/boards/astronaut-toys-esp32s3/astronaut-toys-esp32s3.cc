#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
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
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

#define TAG "AstronautToysESP32S3"

// LV_FONT_DECLARE(font_puhui_14_1);
// LV_FONT_DECLARE(font_awesome_14_1);

class AstronautToysESP32S3 : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button key1_button_;
    Button key2_button_;


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

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
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
            // if (display_) {
            //     display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            // }
        });
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            // if (display_) {
            //      display_->ShowNotification(Lang::Strings::MAX_VOLUME);
            // }
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            // if (display_) {
            //      display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            // }
        });
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            // if (display_) {
            //      display_->ShowNotification(Lang::Strings::MUTED);
            // }
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
        // key2_button_.OnClick([]() {
        //     auto& app = Application::GetInstance();
        //     // 处理 KEY2 点击事件
        //     ESP_LOGI(TAG, "KEY2 Clicked - Placeholder action");
        // });
        // key2_button_.OnDoubleClick([]() {
        //     auto& app = Application::GetInstance();
        //     // 处理 KEY2 双击事件
        //     ESP_LOGI(TAG, "KEY2 Double Clicked - Placeholder action");
        // });
    }

    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker")); 
    }

public:
    AstronautToysESP32S3() : 
    boot_button_(BOOT_BUTTON_GPIO),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
    key1_button_(KEY1_BUTTON_GPIO),
    key2_button_(KEY2_BUTTON_GPIO) {  
        InitializeCodecI2c();
        InitializeButtons();
        InitializeIot();
    }

    virtual Led* GetLed() override {
        static SingleLed led_strip(BUILTIN_LED_GPIO);
        return &led_strip;
    }


    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

};

DECLARE_BOARD(AstronautToysESP32S3);
