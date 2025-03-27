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

// 自定义ES8311音频解码器类，支持双声道输出
class SparkSpotEs8311AudioCodec : public Es8311AudioCodec {
private:
    bool initialization_failed_ = false;
    AudioPowerControlCallback power_control_cb_ = nullptr;

public:
    bool IsInitializationFailed() const { return initialization_failed_; }

    // 构造函数，设置为无MCLK双声道模式
    SparkSpotEs8311AudioCodec(void* i2c_dev, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                      gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                      gpio_num_t pa_pin, uint8_t es8311_addr, AudioPowerControlCallback power_cb)
        : Es8311AudioCodec(i2c_dev, i2c_port, input_sample_rate, output_sample_rate, 
                          mclk, bclk, ws, dout, din, pa_pin, es8311_addr, false) { // use_mclk=false
        
        power_control_cb_ = power_cb;
        input_channels_ = 2; // 设置为双声道
        
        ESP_LOGI(TAG, "Creating SparkSpotEs8311AudioCodec instance (stereo mode)");
        
        if (power_control_cb_ == nullptr) {
            ESP_LOGW(TAG, "No power control callback provided!");
        }
    }

    // 创建双声道I2S通道
    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) override {
        assert(input_sample_rate_ == output_sample_rate_);

        ESP_LOGI(TAG, "Creating I2S channels with parameters:");
        ESP_LOGI(TAG, "  Sample rate: %d Hz", input_sample_rate_);
        ESP_LOGI(TAG, "  MCLK: %s (GPIO %d)", mclk == GPIO_NUM_NC ? "Disabled" : "Enabled", mclk);
        ESP_LOGI(TAG, "  BCLK: GPIO %d, WS: GPIO %d", bclk, ws);
        ESP_LOGI(TAG, "  DOUT: GPIO %d, DIN: GPIO %d", dout, din);
        ESP_LOGI(TAG, "  Mode: Stereo (2 channels)");

        i2s_chan_config_t chan_cfg = {
            .id = I2S_NUM_0,
            .role = I2S_ROLE_MASTER,  // ESP32-S3为主设备
            .dma_desc_num = 6,
            .dma_frame_num = 240,
            .auto_clear_after_cb = true,
            .auto_clear_before_cb = false,
            .intr_priority = 0,
        };
        
        esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2S channels: %s (0x%x)", esp_err_to_name(ret), ret);
            assert(false);
            return;
        }
        
        i2s_std_config_t std_cfg = {
            .clk_cfg = {
                .sample_rate_hz = (uint32_t)output_sample_rate_,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = mclk != GPIO_NUM_NC ? I2S_MCLK_MULTIPLE_256 : I2S_MCLK_MULTIPLE_384,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_STEREO,  // 设置为双声道模式
                .slot_mask = I2S_STD_SLOT_BOTH,     // 使用左右两个声道
                .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
                .ws_pol = false,
                .bit_shift = true,
            },
            .gpio_cfg = {
                .mclk = mclk,
                .bclk = bclk,
                .ws = ws,
                .dout = dout,
                .din = din,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false
                }
            }
        };

        ret = i2s_channel_init_std_mode(tx_handle_, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init TX channel: %s (0x%x)", esp_err_to_name(ret), ret);
            assert(false);
            return;
        }
        
        ret = i2s_channel_init_std_mode(rx_handle_, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init RX channel: %s (0x%x)", esp_err_to_name(ret), ret);
            assert(false);
            return;
        }
        
        // 启用TX和RX通道
        ret = i2s_channel_enable(tx_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable TX channel: %s (0x%x)", esp_err_to_name(ret), ret);
        }
        
        ret = i2s_channel_enable(rx_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable RX channel: %s (0x%x)", esp_err_to_name(ret), ret);
        }
        
        if (mclk == GPIO_NUM_NC) {
            ESP_LOGI(TAG, "Stereo I2S channels created without MCLK");
        } else {
            ESP_LOGI(TAG, "I2S channels created with MCLK");
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
            
            esp_codec_dev_sample_info_t fs = {
                .bits_per_sample = 16,
                .channel = 2,  // 双声道
                .channel_mask = ESP_CODEC_DEV_CHANNEL_BOTH,  // 使用两个声道
                .sample_rate = (uint32_t)input_sample_rate_,
                .mclk_multiple = 0,  // 不使用MCLK
            };
            
            esp_err_t ret = esp_codec_dev_open(input_dev_, &fs);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open input device: %s (0x%x)", esp_err_to_name(ret), ret);
                return;
            }
            
            ret = esp_codec_dev_set_in_gain(input_dev_, 40.0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set input gain: %s (0x%x)", esp_err_to_name(ret), ret);
            }
        } else {
            esp_err_t ret = esp_codec_dev_close(input_dev_);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to close input device: %s (0x%x)", esp_err_to_name(ret), ret);
            }
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!output_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_ && power_control_cb_) {
                    power_control_cb_(false);
                }
            }
        }
        
        input_enabled_ = enable;
        ESP_LOGI(TAG, "Input %s", enable ? "enabled" : "disabled");
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
            
            esp_codec_dev_sample_info_t fs = {
                .bits_per_sample = 16,
                .channel = 2,  // 双声道
                .channel_mask = ESP_CODEC_DEV_CHANNEL_BOTH,  // 使用两个声道
                .sample_rate = (uint32_t)output_sample_rate_,
                .mclk_multiple = 0,  // 不使用MCLK
            };
            
            esp_err_t ret = esp_codec_dev_open(output_dev_, &fs);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open output device: %s (0x%x)", esp_err_to_name(ret), ret);
                return;
            }
            
            ret = esp_codec_dev_set_out_vol(output_dev_, output_volume_);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set output volume: %s (0x%x)", esp_err_to_name(ret), ret);
            }
            
            if (pa_pin_ != GPIO_NUM_NC) {
                gpio_set_level(pa_pin_, 1);
            }
        } else {
            if (pa_pin_ != GPIO_NUM_NC) {
                gpio_set_level(pa_pin_, 0);
            }
            
            esp_err_t ret = esp_codec_dev_close(output_dev_);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to close output device: %s (0x%x)", esp_err_to_name(ret), ret);
            }
            
            // 如果输入输出都禁用，考虑关闭电源
            if (!input_enabled_) {
                // 延迟500ms后关闭，避免频繁开关
                vTaskDelay(pdMS_TO_TICKS(500));
                if (!input_enabled_ && !output_enabled_ && power_control_cb_) {
                    power_control_cb_(false);
                }
            }
        }
        
        output_enabled_ = enable;
        ESP_LOGI(TAG, "Output %s", enable ? "enabled" : "disabled");
    }

    int Read(int16_t* dest, int samples) override {
        if (input_enabled_) {
            // 注意：samples是样本数，双声道下实际数据大小需要乘以通道数
            esp_err_t ret = esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t) * input_channels_);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Error reading from codec: %s (0x%x)", esp_err_to_name(ret), ret);
            }
        }
        return samples;
    }

    int Write(const int16_t* data, int samples) override {
        if (output_enabled_) {
            // 注意：samples是样本数，双声道下实际数据大小需要乘以通道数
            esp_err_t ret = esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t) * input_channels_);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Error writing to codec: %s (0x%x)", esp_err_to_name(ret), ret);
            }
        }
        return samples;
    }

    ~SparkSpotEs8311AudioCodec() {
        ESP_LOGI(TAG, "Destroying SparkSpotEs8311AudioCodec...");
    }
};

// ESP-SparkSpot主板类
class EspSparkSpotBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    Button boot_button_;
    SparkSpotEs8311AudioCodec* audio_codec_ptr_ = nullptr;
    
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
        i2c_master_bus_config_t i2c_mst_config = {
            .i2c_port = I2C_PORT_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        
        ESP_LOGI(TAG, "Creating I2C master bus with config:");
        ESP_LOGI(TAG, "  Port: %d, SCL: %d, SDA: %d", 
                i2c_mst_config.i2c_port, 
                i2c_mst_config.scl_io_num, 
                i2c_mst_config.sda_io_num);
        
        esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_);
        ESP_ERROR_CHECK(ret);
        ESP_LOGI(TAG, "I2C master bus created");
        
        // 创建I2C设备
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AUDIO_CODEC_ES8311_ADDR,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };
        
        ESP_LOGI(TAG, "Adding I2C device with config:");
        ESP_LOGI(TAG, "  Address: 0x%02x, Speed: %lu Hz", 
                dev_cfg.device_address, 
                dev_cfg.scl_speed_hz);
        
        ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
        ESP_ERROR_CHECK(ret);
        ESP_LOGI(TAG, "I2C device added");
    }

    // 测试I2C通信
    bool TestI2cCommunication() {
        ESP_LOGI(TAG, "Testing I2C communication with ES8311...");
        uint8_t reg_addr = 0xFD; // ES8311 Chip ID1 register
        uint8_t reg_data = 0;
        esp_err_t ret;
        
        ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C write failed: %s (0x%x)", esp_err_to_name(ret), ret);
            return false;
        }
        
        ret = i2c_master_receive(i2c_dev_, &reg_data, 1, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C read failed: %s (0x%x)", esp_err_to_name(ret), ret);
            return false;
        }
        
        ESP_LOGI(TAG, "ES8311 chip ID read: 0x%02x (expected 0x83)", reg_data);
        return (reg_data == 0x83);
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
        
        if (!TestI2cCommunication()) {
            ESP_LOGE(TAG, "Failed to communicate with ES8311 codec!");
        }
        
        InitializeButtons();
        InitializeIot();
        
        // 创建音频编解码器实例
        audio_codec_ptr_ = new SparkSpotEs8311AudioCodec(
            (void*)i2c_dev_, I2C_PORT_NUM, 
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR,
            GlobalAudioPowerControl);
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
        return audio_codec_ptr_;
    }
    
    virtual ~EspSparkSpotBoard() {
        if (audio_codec_ptr_) {
            delete audio_codec_ptr_;
            audio_codec_ptr_ = nullptr;
        }
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