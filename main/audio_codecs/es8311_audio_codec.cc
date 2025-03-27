#include "es8311_audio_codec.h"

#include <esp_log.h>

// 添加通道掩码定义，因为ESP-IDF版本可能没有这个定义
#ifndef ESP_CODEC_DEV_CHANNEL_LEFT
#define ESP_CODEC_DEV_CHANNEL_LEFT 0x01  // 左声道掩码
#endif

static const char TAG[] = "Es8311AudioCodec";

Es8311AudioCodec::Es8311AudioCodec(void* i2c_dev_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8311_addr, bool use_mclk) {
    ESP_LOGI(TAG, "Initializing ES8311 audio codec...");
    ESP_LOGI(TAG, "Parameters: I2C port: %d, ES8311 addr: 0x%02x, Use MCLK: %s", 
             i2c_port, es8311_addr, use_mclk ? "yes" : "no");
    ESP_LOGI(TAG, "Sample rates - Input: %d Hz, Output: %d Hz", input_sample_rate, output_sample_rate);
    
    duplex_ = true; // 是否双工
    input_reference_ = false; // 是否使用参考输入，实现回声消除
    input_channels_ = 1; // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    pa_pin_ = pa_pin;
    
    // 创建I2S通道
    ESP_LOGI(TAG, "Creating I2S channels...");
    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // 创建I2S数据接口
    ESP_LOGI(TAG, "Creating I2S data interface...");
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    if (data_if_ == NULL) {
        ESP_LOGE(TAG, "Failed to create I2S data interface!");
        return;
    }
    ESP_LOGI(TAG, "I2S data interface created successfully");

    // 创建I2C控制接口
    ESP_LOGI(TAG, "Creating I2C control interface...");
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = i2c_port,
        .addr = es8311_addr,
        .bus_handle = i2c_dev_handle,
    };
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if_ == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C control interface!");
        // 释放已分配的资源
        audio_codec_delete_data_if(data_if_);
        data_if_ = NULL;
        return;
    }
    ESP_LOGI(TAG, "I2C control interface created successfully");

    // 创建GPIO接口
    ESP_LOGI(TAG, "Creating GPIO interface...");
    gpio_if_ = audio_codec_new_gpio();
    if (gpio_if_ == NULL) {
        ESP_LOGE(TAG, "Failed to create GPIO interface!");
        // 释放已分配的资源
        audio_codec_delete_data_if(data_if_);
        audio_codec_delete_ctrl_if(ctrl_if_);
        data_if_ = NULL;
        ctrl_if_ = NULL;
        return;
    }
    ESP_LOGI(TAG, "GPIO interface created successfully");

    // 创建ES8311编解码器接口
    ESP_LOGI(TAG, "Creating ES8311 codec interface...");
    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = ctrl_if_;
    es8311_cfg.gpio_if = gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = use_mclk;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    
    // 尝试创建ES8311编解码器接口
    codec_if_ = es8311_codec_new(&es8311_cfg);
    if (codec_if_ == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface!");
        // 释放已分配的资源
        audio_codec_delete_data_if(data_if_);
        audio_codec_delete_ctrl_if(ctrl_if_);
        audio_codec_delete_gpio_if(gpio_if_);
        data_if_ = NULL;
        ctrl_if_ = NULL;
        gpio_if_ = NULL;
        return;
    }
    ESP_LOGI(TAG, "ES8311 codec interface created successfully");

    // 创建输出设备
    ESP_LOGI(TAG, "Creating output device...");
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    if (output_dev_ == NULL) {
        ESP_LOGE(TAG, "Failed to create output device!");
        // 释放已分配的资源
        audio_codec_delete_codec_if(codec_if_);
        audio_codec_delete_data_if(data_if_);
        audio_codec_delete_ctrl_if(ctrl_if_);
        audio_codec_delete_gpio_if(gpio_if_);
        codec_if_ = NULL;
        data_if_ = NULL;
        ctrl_if_ = NULL;
        gpio_if_ = NULL;
        return;
    }
    
    // 创建输入设备
    ESP_LOGI(TAG, "Creating input device...");
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    if (input_dev_ == NULL) {
        ESP_LOGE(TAG, "Failed to create input device!");
        // 释放已分配的资源
        esp_codec_dev_delete(output_dev_);
        audio_codec_delete_codec_if(codec_if_);
        audio_codec_delete_data_if(data_if_);
        audio_codec_delete_ctrl_if(ctrl_if_);
        audio_codec_delete_gpio_if(gpio_if_);
        output_dev_ = NULL;
        codec_if_ = NULL;
        data_if_ = NULL;
        ctrl_if_ = NULL;
        gpio_if_ = NULL;
        return;
    }
    
    // 配置设备不在关闭时禁用
    ESP_LOGI(TAG, "Configuring devices...");
    esp_codec_set_disable_when_closed(output_dev_, false);
    esp_codec_set_disable_when_closed(input_dev_, false);
    
    ESP_LOGI(TAG, "ES8311 audio codec initialized successfully");
}

Es8311AudioCodec::~Es8311AudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(codec_if_);
    audio_codec_delete_ctrl_if(ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void Es8311AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    ESP_LOGI(TAG, "Creating I2S channels with parameters:");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", input_sample_rate_);
    ESP_LOGI(TAG, "  MCLK: %s (GPIO %d)", mclk == GPIO_NUM_NC ? "Disabled" : "Enabled", mclk);
    ESP_LOGI(TAG, "  BCLK: GPIO %d, WS: GPIO %d", bclk, ws);
    ESP_LOGI(TAG, "  DOUT: GPIO %d, DIN: GPIO %d", dout, din);

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
    ESP_LOGI(TAG, "I2S channels created successfully");

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,  // 16kHz
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = mclk != GPIO_NUM_NC ? I2S_MCLK_MULTIPLE_256 : I2S_MCLK_MULTIPLE_384,  // 不同的MCLK倍数配置
			#ifdef   I2S_HW_VERSION_2    
				.ext_clk_freq_hz = 0,
			#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,  // 明确指定为单声道模式
            .slot_mask = I2S_STD_SLOT_LEFT,   // 仅使用左声道
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            #ifdef   I2S_HW_VERSION_2   
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false
            #endif
        },
        .gpio_cfg = {
            .mclk = mclk,  // 传入GPIO_NUM_NC表示不使用MCLK
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
        ESP_LOGI(TAG, "Mono I2S channels created without MCLK");
    } else {
        ESP_LOGI(TAG, "I2S channels created with MCLK");
    }
}

void Es8311AudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void Es8311AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        ESP_LOGI(TAG, "Input already %s, skipping", enable ? "enabled" : "disabled");
        return;
    }
    if (enable) {
        ESP_LOGI(TAG, "Enabling ES8311 input (microphone)");
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1, 
            .channel_mask = ESP_CODEC_DEV_CHANNEL_LEFT,  // 明确指定使用左声道
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,  // 不使用MCLK
        };
        
        esp_err_t ret = esp_codec_dev_open(input_dev_, &fs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open input device: %s (0x%x)", esp_err_to_name(ret), ret);
            return;
        }
        ESP_LOGI(TAG, "Input device opened successfully");
        
        ret = esp_codec_dev_set_in_gain(input_dev_, 40.0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set input gain: %s (0x%x)", esp_err_to_name(ret), ret);
        } else {
            ESP_LOGI(TAG, "Input gain set to 40.0 dB");
        }
    } else {
        ESP_LOGI(TAG, "Disabling ES8311 input (microphone)");
        esp_err_t ret = esp_codec_dev_close(input_dev_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to close input device: %s (0x%x)", esp_err_to_name(ret), ret);
        } else {
            ESP_LOGI(TAG, "Input device closed successfully");
        }
    }
    AudioCodec::EnableInput(enable);
    ESP_LOGI(TAG, "Input %s", enable ? "enabled" : "disabled");
}

void Es8311AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        ESP_LOGI(TAG, "Output already %s, skipping", enable ? "enabled" : "disabled");
        return;
    }
    if (enable) {
        ESP_LOGI(TAG, "Enabling ES8311 output (speaker)");
        // Play 16bit 1 channel
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,  
            .channel_mask = ESP_CODEC_DEV_CHANNEL_LEFT,  // 明确指定使用左声道
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,  // 不使用MCLK
        };
        
        esp_err_t ret = esp_codec_dev_open(output_dev_, &fs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open output device: %s (0x%x)", esp_err_to_name(ret), ret);
            return;
        }
        ESP_LOGI(TAG, "Output device opened successfully");
        
        ret = esp_codec_dev_set_out_vol(output_dev_, output_volume_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set output volume: %s (0x%x)", esp_err_to_name(ret), ret);
        } else {
            ESP_LOGI(TAG, "Output volume set to %d", output_volume_);
        }
        
        if (pa_pin_ != GPIO_NUM_NC) {
            ESP_LOGI(TAG, "Enabling PA on GPIO %d", pa_pin_);
            gpio_set_level(pa_pin_, 1);
        }
    } else {
        ESP_LOGI(TAG, "Disabling ES8311 output (speaker)");
        esp_err_t ret = esp_codec_dev_close(output_dev_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to close output device: %s (0x%x)", esp_err_to_name(ret), ret);
        } else {
            ESP_LOGI(TAG, "Output device closed successfully");
        }
        
        if (pa_pin_ != GPIO_NUM_NC) {
            ESP_LOGI(TAG, "Disabling PA on GPIO %d", pa_pin_);
            gpio_set_level(pa_pin_, 0);
        }
    }
    AudioCodec::EnableOutput(enable);
    ESP_LOGI(TAG, "Output %s", enable ? "enabled" : "disabled");
}

int Es8311AudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        esp_err_t ret = esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Error reading from codec: %s (0x%x)", esp_err_to_name(ret), ret);
        }
    }
    return samples;
}

int Es8311AudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_) {
        esp_err_t ret = esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Error writing to codec: %s (0x%x)", esp_err_to_name(ret), ret);
        }
    }
    return samples;
}