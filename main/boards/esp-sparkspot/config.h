#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// ESP-SparkSpot Board configuration

#include <driver/gpio.h>

// 音频配置
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// I2S 引脚配置
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC  // MCLK不连接
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_17  // LRCK/WS引脚
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_16  // SCLK/BCLK引脚
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_15  // SDOUT引脚（ES8311输出至ESP32）
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_18  // SDIN引脚（ESP32输出至ES8311）

// 音频编解码器配置
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_40  // 功放使能控制引脚
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_2   // I2C数据线
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1   // I2C时钟线
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR         // CE引脚接地时的I2C地址
#define AUDIO_PREP_VCC_CTL       GPIO_NUM_6   // 音频模块供电使能端

// LED和按钮配置
#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// 其他配置
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000

#endif // _BOARD_CONFIG_H_ 