#ifndef _BREAD_ES8311_AUDIO_CODEC_H_
#define _BREAD_ES8311_AUDIO_CODEC_H_

#include "audio_codecs/es8311_audio_codec.h"
#include <driver/i2c_master.h>
#include <esp_log.h>

// 声明音频电源控制回调类型
typedef void (*AudioPowerControlCallback)(bool enable);

// 声明全局音频电源控制回调函数
void GlobalAudioPowerControl(bool enable);

// BreadES8311AudioCodec类 - 为bread-compact-wifi板定制的ES8311音频编解码器实现
class BreadES8311AudioCodec : public Es8311AudioCodec {
private:
    static constexpr const char* TAG = "BreadES8311";
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_dev_ = nullptr;
    bool initialization_failed_ = false;
    AudioPowerControlCallback power_control_cb_ = nullptr;

    // 初始化I2C总线
    bool InitializeI2c();
    
    // 测试I2C通信
    bool TestI2cCommunication();

public:
    BreadES8311AudioCodec(int input_sample_rate, int output_sample_rate,
                     gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                     gpio_num_t dout, gpio_num_t din,
                     gpio_num_t pa_pin, AudioPowerControlCallback power_cb);
                     
    // 启用/禁用音频输入
    void EnableInput(bool enable) override;
    
    // 启用/禁用音频输出
    void EnableOutput(bool enable) override;
    
    // 判断初始化是否失败
    bool IsInitializationFailed() const { return initialization_failed_; }
    
    // 析构函数
    virtual ~BreadES8311AudioCodec();
};

#endif // _BREAD_ES8311_AUDIO_CODEC_H_ 