#ifndef _ES8311_AUDIO_CODEC_H
#define _ES8311_AUDIO_CODEC_H

#include "audio_codec.h"

#include <driver/i2c.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>

// ES8311寄存器定义
#define ES8311_RESET_REG00             0x00  // 复位控制寄存器
#define ES8311_CLK_MANAGER_REG01       0x01  // 时钟管理寄存器1
#define ES8311_CLK_MANAGER_REG02       0x02  // 时钟管理寄存器2
#define ES8311_CLK_MANAGER_REG03       0x03  // 时钟管理寄存器3
#define ES8311_ADC_OSR_REG04           0x04  // ADC过采样率寄存器
#define ES8311_DAC_OSR_REG05           0x05  // DAC过采样率寄存器
#define ES8311_SDPIN_REG06             0x06  // 串行数据引脚配置寄存器
#define ES8311_SDPOUT_REG07            0x07  // 串行数据输出配置寄存器
#define ES8311_SYSTEM_REG08            0x08  // 系统配置寄存器
#define ES8311_SYSTEM_REG09            0x09  // 系统配置寄存器2
#define ES8311_SYSTEM_REG0A            0x0A  // 系统配置寄存器3
#define ES8311_SYSTEM_REG0B            0x0B  // 系统配置寄存器4
#define ES8311_ADC_REG10               0x10  // ADC电源管理寄存器
#define ES8311_ADC_REG11               0x11  // ADC数字音量控制
#define ES8311_ADC_REG12               0x12  // ADC静音控制寄存器
#define ES8311_ADC_REG13               0x13  // ADC ALC控制寄存器1
#define ES8311_ADC_REG14               0x14  // ADC ALC控制寄存器2
#define ES8311_ADC_REG15               0x15  // ADC ALC控制寄存器3
#define ES8311_ADC_REG16               0x16  // ADC ALC控制寄存器4
#define ES8311_ADC_REG17               0x17  // ADC ALC控制寄存器5
#define ES8311_DAC_REG31               0x31  // DAC电源管理寄存器
#define ES8311_DAC_REG32               0x32  // DAC控制寄存器1
#define ES8311_DAC_REG33               0x33  // DAC控制寄存器2
#define ES8311_DAC_REG34               0x34  // DAC控制寄存器3
#define ES8311_DAC_REG35               0x35  // DAC音量控制
#define ES8311_DAC_REG37               0x37  // DAC静音控制寄存器

// ES8311工作模式
enum es8311_mode_t {
    ES8311_MODE_ADC = 0x01,  // 仅ADC模式
    ES8311_MODE_DAC = 0x02,  // 仅DAC模式
    ES8311_MODE_BOTH = 0x03  // ADC和DAC同时工作
};

// ES8311音频增益配置
typedef struct {
    float pa_voltage;          // 功率放大器工作电压
    float codec_dac_voltage;   // 编解码器DAC输出电压
} es8311_hw_gain_t;

class Es8311AudioCodec : public AudioCodec {
private:
    i2c_master_bus_handle_t i2c_bus_handle_;
    i2c_master_dev_handle_t i2c_dev_handle_;
    uint8_t i2c_address_;
    gpio_num_t pa_pin_;
    bool use_mclk_;
    es8311_mode_t codec_mode_;
    
    // 内部辅助方法
    bool WriteReg(uint8_t reg_addr, uint8_t data);
    bool ReadReg(uint8_t reg_addr, uint8_t* data);
    bool InitCodec();
    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);

    // AudioCodec接口实现
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    Es8311AudioCodec(i2c_master_bus_handle_t i2c_bus_handle, uint8_t i2c_address, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        gpio_num_t pa_pin, bool use_mclk = true);
    virtual ~Es8311AudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _ES8311_AUDIO_CODEC_H