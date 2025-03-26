#include "es8311_audio_codec.h"
#include <esp_log.h>
#include <driver/i2c.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>

static const char* TAG = "ES8311";

// 构造函数
Es8311AudioCodec::Es8311AudioCodec(i2c_master_bus_handle_t i2c_bus_handle, uint8_t i2c_address, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, bool use_mclk)
    : i2c_bus_handle_(i2c_bus_handle)
    , i2c_address_(i2c_address)
    , pa_pin_(pa_pin)
    , use_mclk_(use_mclk)
    , codec_mode_(ES8311_MODE_BOTH) {
    
    ESP_LOGI(TAG, "初始化ES8311, I2C地址: 0x%02X, PA引脚: %d", i2c_address, pa_pin);
    
    // 设置基本参数
    duplex_ = true;                       // 设置为双工模式，同时支持录音和播放
    input_reference_ = false;             // 不使用参考输入（用于回声消除）
    input_channels_ = 1;                  // 输入通道数为单声道
    input_sample_rate_ = input_sample_rate;  // 设置输入采样率
    output_sample_rate_ = output_sample_rate; // 设置输出采样率
    
    // 配置I2C设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address_,
        .scl_speed_hz = 400000,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle_, &dev_cfg, &i2c_dev_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法添加ES8311 I2C设备: %s", esp_err_to_name(ret));
        return;
    }
    
    // 配置功率放大器引脚（如果有）
    if (pa_pin_ != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << pa_pin_);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(pa_pin_, 0);  // 初始关闭功放
        ESP_LOGI(TAG, "功放控制引脚已初始化: %d", pa_pin_);
    }
    
    // 创建I2S通道
    CreateDuplexChannels(mclk, bclk, ws, dout, din);
    
    // 初始化ES8311编解码器
    InitCodec();
    
    ESP_LOGI(TAG, "ES8311音频编解码器已初始化");
}

Es8311AudioCodec::~Es8311AudioCodec() {
    // 关闭功率放大器
    if (pa_pin_ != GPIO_NUM_NC) {
        gpio_set_level(pa_pin_, 0);
    }
    
    // 删除I2C设备
    if (i2c_dev_handle_) {
        i2c_master_bus_rm_device(i2c_dev_handle_);
    }
    
    // 关闭I2S通道
    if (tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
    }
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
    }
}

// 实现I2C寄存器读写 - 使用新版ESP-IDF I2C主机总线API
bool Es8311AudioCodec::WriteReg(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle_, write_buf, sizeof(write_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写寄存器失败: 0x%02X = 0x%02X, err = %d", reg_addr, data, ret);
        return false;
    }
    return true;
}

bool Es8311AudioCodec::ReadReg(uint8_t reg_addr, uint8_t* data) {
    if (data == nullptr) {
        return false;
    }

    // 先写寄存器地址
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle_, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写寄存器地址失败: 0x%02X, err = %d", reg_addr, ret);
        return false;
    }
    
    // 然后读取数据
    ret = i2c_master_receive(i2c_dev_handle_, data, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读寄存器失败: 0x%02X, err = %d", reg_addr, ret);
        return false;
    }
    return true;
}

// 初始化ES8311编解码器
bool Es8311AudioCodec::InitCodec() {
    ESP_LOGI(TAG, "初始化ES8311编解码器...");
    
    // 复位编解码器
    WriteReg(ES8311_RESET_REG00, 0x1F);        // 复位所有寄存器
    vTaskDelay(pdMS_TO_TICKS(20));             // 等待复位完成
    WriteReg(ES8311_RESET_REG00, 0x00);        // 退出复位状态
    
    // 1. 配置时钟管理 - ES8311从时钟模式
    ESP_LOGI(TAG, "ES8311配置为从时钟模式（从I2S获取时钟）");
    WriteReg(ES8311_CLK_MANAGER_REG01, 0x3F);  // 从时钟模式，使用BCLK作为时钟源
    WriteReg(ES8311_CLK_MANAGER_REG02, 0x00);  // LRCK DIV配置为256fs
    WriteReg(ES8311_CLK_MANAGER_REG03, 0x00);  // 从时钟模式，不需要分频
    
    // 2. 配置格式 - I2S, 16bit
    WriteReg(ES8311_SDPIN_REG06, 0x02);        // I2S模式，16bit
    WriteReg(ES8311_SDPOUT_REG07, 0x02);       // I2S模式，16bit
    
    // 3. 配置ADC
    WriteReg(ES8311_SYSTEM_REG0A, 0x00);       // ADC时钟源为CLK1
    WriteReg(ES8311_SYSTEM_REG0B, 0x00);       // DAC时钟源为CLK1
    WriteReg(ES8311_ADC_REG10, 0x0C);          // 使能ADC模块电源
    WriteReg(ES8311_ADC_REG11, 0x48);          // ADC增益为0dB
    WriteReg(ES8311_ADC_REG12, 0x00);          // ADC未静音
    WriteReg(ES8311_ADC_REG13, 0x10);          // ALC设置
    WriteReg(ES8311_ADC_REG14, 0x16);          // ALC设置
    WriteReg(ES8311_ADC_REG15, 0x00);          // ALC设置
    WriteReg(ES8311_ADC_REG16, 0x00);          // ALC设置
    WriteReg(ES8311_ADC_REG17, 0xC8);          // ALC设置
    
    // 4. 配置DAC
    WriteReg(ES8311_DAC_REG31, 0x00);          // 使能DAC模块电源
    WriteReg(ES8311_DAC_REG32, 0x00);          // DAC时钟源为CLK1
    WriteReg(ES8311_DAC_REG33, 0x00);          // DAC未静音
    WriteReg(ES8311_DAC_REG34, 0x00);          // DAC左右声道音量设置
    WriteReg(ES8311_DAC_REG35, 0x00);          // DAC音量为0dB
    WriteReg(ES8311_DAC_REG37, 0x00);          // DAC控制寄存器
    
    // 5. 最后，启用模式选择
    uint8_t mode_reg = 0x00;
    if (codec_mode_ & ES8311_MODE_ADC) {
        mode_reg |= 0x10;  // 启用ADC
    }
    if (codec_mode_ & ES8311_MODE_DAC) {
        mode_reg |= 0x01;  // 启用DAC
    }
    WriteReg(ES8311_SYSTEM_REG08, mode_reg);  // 设置工作模式
    WriteReg(ES8311_SYSTEM_REG09, 0x00);      // 取消静音
    
    ESP_LOGI(TAG, "ES8311初始化完成，模式: 0x%02X", codec_mode_);
    return true;
}

void Es8311AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    // ESP32-S3作为主设备，提供BCLK和LRCK
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,  // ESP32-S3作为主设备
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = static_cast<uint32_t>(output_sample_rate_),
            .clk_src = I2S_CLK_SRC_DEFAULT,  // 使用ESP32-S3的内部时钟源
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = 16,
            .ws_pol = false,
            .bit_shift = false,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,  // 不使用MCLK
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "创建I2S通道: ESP32-S3主模式, 采样率: %d Hz", output_sample_rate_);
    
    // 启动I2S通道
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
}

void Es8311AudioCodec::SetOutputVolume(int volume) {
    // 限制音量在0-100范围内
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    // 将0-100范围转换为ES8311的0-255范围 (0x00-0xFF)
    // ES8311的音量是反向的，0xFF是最小音量，0x00是最大音量
    uint8_t es8311_volume = 0xFF - (volume * 255 / 100);
    
    // 设置ES8311的DAC音量寄存器
    WriteReg(ES8311_DAC_REG35, es8311_volume);
    
    // 调用父类方法保存设置
    AudioCodec::SetOutputVolume(volume);
}

void Es8311AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    
    if (enable) {
        // 启用ADC
        WriteReg(ES8311_SYSTEM_REG08, codec_mode_ | 0x10); // 设置ADC工作模式
        WriteReg(ES8311_ADC_REG12, 0x00); // 取消ADC静音
    } else {
        // 禁用ADC
        WriteReg(ES8311_ADC_REG12, 0x01); // 设置ADC静音
        WriteReg(ES8311_SYSTEM_REG08, codec_mode_ & ~0x10); // 关闭ADC
    }
    
    // 调用父类方法更新状态
    AudioCodec::EnableInput(enable);
}

void Es8311AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    
    if (enable) {
        // 启用DAC
        WriteReg(ES8311_SYSTEM_REG08, codec_mode_ | 0x01); // 设置DAC工作模式
        WriteReg(ES8311_DAC_REG37, 0x00); // 取消DAC静音
        
        // 控制功率放大器
        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 1);
        }
    } else {
        // 禁用DAC
        WriteReg(ES8311_DAC_REG37, 0x01); // 设置DAC静音
        WriteReg(ES8311_SYSTEM_REG08, codec_mode_ & ~0x01); // 关闭DAC
        
        // 关闭功率放大器
        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 0);
        }
    }
    
    // 调用父类方法更新状态
    AudioCodec::EnableOutput(enable);
}

int Es8311AudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || !dest || samples <= 0) {
        return 0;
    }
    
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(rx_handle_, dest, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S读取失败: %d", ret);
        return 0;
    }
    
    return bytes_read / sizeof(int16_t);
}

int Es8311AudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || !data || samples <= 0) {
        return 0;
    }
    
    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(tx_handle_, data, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S写入失败: %d", ret);
        return 0;
    }
    
    return bytes_written / sizeof(int16_t);
}