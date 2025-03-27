#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>

#define TAG "ESP-SparkSpot"

// 音频电源控制的全局回调函数
void GlobalAudioPowerControl(bool enable);

// ESP-SparkSpot主板类
class EspSparkSpotBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    Button boot_button_;
    bool es8311_detected_ = false;
    
    // 初始化音频电源控制
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

    // 初始化I2C总线
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1
            }
        };
        
        ESP_LOGI(TAG, "Creating I2C master bus with config:");
        ESP_LOGI(TAG, "  Port: %d, SCL: %d, SDA: %d", 
                i2c_bus_cfg.i2c_port, 
                i2c_bus_cfg.scl_io_num, 
                i2c_bus_cfg.sda_io_num);
        
        esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_);
        ESP_ERROR_CHECK(ret);
        ESP_LOGI(TAG, "I2C master bus created");
    }

    // 检测I2C设备
    void I2cDetect() {
        ESP_LOGI(TAG, "Scanning I2C bus for devices...");
        uint8_t address;
        
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                    if (address == AUDIO_CODEC_ES8311_ADDR) {
                        // ESP_LOGI(TAG, "ES8311 audio codec detected at address 0x%02x", address);
                        es8311_detected_ = true;
                    }
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
        
        if (!es8311_detected_) {
            ESP_LOGW(TAG, "ES8311 audio codec NOT detected!");
        }
    }

    // 初始化按钮
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
    }

public:
    EspSparkSpotBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeAudioPower();
        InitializeI2c();
        
        // 扫描I2C总线并检测设备
        I2cDetect();
        
        InitializeButtons();
        InitializeIot();
        
        ESP_LOGI(TAG, "EspSparkSpotBoard initialized");
    }
    
    // 电源控制方法
    void SetAudioPower(bool enable) {
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_PREP_VCC_CTL, enable ? 1 : 0));
        ESP_LOGI(TAG, "Audio power %s", enable ? "enabled" : "disabled");
        
        if (enable) {
            // 给电路足够的上电稳定时间
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        // 采用静态实例，简化音频编解码器创建
        static Es8311AudioCodec audio_codec(
            i2c_bus_, I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, false); // use_mclk=false
        
        return &audio_codec;
    }
    
    virtual ~EspSparkSpotBoard() {
        // 不需要释放audio_codec，因为它是静态实例
    }
};

// 获取ESP-SparkSpot实例的辅助函数
static EspSparkSpotBoard* g_board_instance = nullptr;
EspSparkSpotBoard* GetEspSparkSpotBoard() {
    if (!g_board_instance) {
        g_board_instance = static_cast<EspSparkSpotBoard*>(&Board::GetInstance());
    }
    return g_board_instance;
}

// 全局音频电源控制回调
void GlobalAudioPowerControl(bool enable) {
    EspSparkSpotBoard* board = GetEspSparkSpotBoard();
    if (board) {
        board->SetAudioPower(enable);
    }
}

DECLARE_BOARD(EspSparkSpotBoard); 