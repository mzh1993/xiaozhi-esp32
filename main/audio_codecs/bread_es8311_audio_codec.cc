#include "audio_codecs/bread_es8311_audio_codec.h"
#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 构造函数
BreadES8311AudioCodec::BreadES8311AudioCodec(int input_sample_rate, int output_sample_rate,
                                     gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                                     gpio_num_t dout, gpio_num_t din,
                                     gpio_num_t pa_pin, AudioPowerControlCallback power_cb)
    : Es8311AudioCodec(nullptr, I2C_PORT_NUM, input_sample_rate, output_sample_rate, 
                      mclk, bclk, ws, dout, din, pa_pin, AUDIO_CODEC_ES8311_ADDR, false) {
    
    ESP_LOGI(TAG, "Initializing BreadES8311AudioCodec...");
    power_control_cb_ = power_cb;
    
    // 初始化I2C
    if (!InitializeI2c()) {
        ESP_LOGE(TAG, "I2C initialization failed!");
        initialization_failed_ = true;
        return;
    }
    
    // 测试I2C通信
    if (!TestI2cCommunication()) {
        ESP_LOGE(TAG, "ES8311 communication test failed!");
        initialization_failed_ = true;
        return;
    }
    
    ESP_LOGI(TAG, "ES8311 initialization completed successfully");
}

// 初始化I2C总线
bool BreadES8311AudioCodec::InitializeI2c() {
    ESP_LOGI(TAG, "Initializing I2C bus (SDA:%d, SCL:%d)", AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
    
    // I2C主机配置
    i2c_master_bus_config_t i2c_mst_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true
        }
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus creation failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // I2C设备配置
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AUDIO_CODEC_ES8311_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    
    ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "I2C initialized successfully");
    return true;
}

// 测试I2C通信
bool BreadES8311AudioCodec::TestI2cCommunication() {
    ESP_LOGI(TAG, "Testing communication with ES8311...");
    
    uint8_t reg_addr = 0xFD; // ES8311 Chip ID1 register
    uint8_t reg_data = 0;
    esp_err_t ret;
    
    ret = i2c_master_transmit(i2c_dev_, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = i2c_master_receive(i2c_dev_, &reg_data, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "ES8311 chip ID: 0x%02x (expected 0x83)", reg_data);
    return (reg_data == 0x83);
}

// 启用/禁用音频输入
void BreadES8311AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    
    if (enable) {
        // 使用回调函数启用音频电源
        if (power_control_cb_) {
            ESP_LOGI(TAG, "Enabling audio power for input");
            power_control_cb_(true);
        }
        
        // 调用父类方法启用输入
        Es8311AudioCodec::EnableInput(enable);
        ESP_LOGI(TAG, "Audio input enabled");
    } else {
        // 调用父类方法禁用输入
        Es8311AudioCodec::EnableInput(enable);
        ESP_LOGI(TAG, "Audio input disabled");
        
        // 如果输入输出都禁用，考虑关闭电源
        if (!output_enabled_ && power_control_cb_) {
            // 延迟500ms后关闭，避免频繁开关
            vTaskDelay(pdMS_TO_TICKS(500));
            if (!input_enabled_ && !output_enabled_) {
                ESP_LOGI(TAG, "Both input and output disabled, turning power off");
                power_control_cb_(false);
            }
        }
    }
}

// 启用/禁用音频输出
void BreadES8311AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    
    if (enable) {
        // 使用回调函数启用音频电源
        if (power_control_cb_) {
            ESP_LOGI(TAG, "Enabling audio power for output");
            power_control_cb_(true);
        }
        
        // 调用父类方法启用输出
        Es8311AudioCodec::EnableOutput(enable);
        ESP_LOGI(TAG, "Audio output enabled");
    } else {
        // 调用父类方法禁用输出
        Es8311AudioCodec::EnableOutput(enable);
        ESP_LOGI(TAG, "Audio output disabled");
        
        // 如果输入输出都禁用，考虑关闭电源
        if (!input_enabled_ && power_control_cb_) {
            // 延迟500ms后关闭，避免频繁开关
            vTaskDelay(pdMS_TO_TICKS(500));
            if (!input_enabled_ && !output_enabled_) {
                ESP_LOGI(TAG, "Both input and output disabled, turning power off");
                power_control_cb_(false);
            }
        }
    }
}

// 析构函数
BreadES8311AudioCodec::~BreadES8311AudioCodec() {
    ESP_LOGI(TAG, "Destroying BreadES8311AudioCodec");
    
    // 释放I2C资源
    if (i2c_dev_ != nullptr) {
        i2c_master_bus_rm_device(i2c_dev_);
        i2c_dev_ = nullptr;
    }
    if (i2c_bus_ != nullptr) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
} 