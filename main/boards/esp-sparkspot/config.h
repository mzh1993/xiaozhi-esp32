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
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR 


// LED和按钮配置
#define BUILTIN_LED_GPIO        GPIO_NUM_11
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#define TOUCH_BUTTON_HEAD_GPIO       GPIO_NUM_3  // 头部触摸按键
#define TOUCH_BUTTON_BELLY_GPIO      GPIO_NUM_9  // 肚子触摸按键
#define TOUCH_BUTTON_TOY_GPIO        GPIO_NUM_13  // 玩具触摸按键
#define TOUCH_BUTTON_FACE_GPIO       GPIO_NUM_14  // 脸部触摸按键
#define TOUCH_BUTTON_LEFT_HAND_GPIO  GPIO_NUM_21  // 左手触摸按键
#define TOUCH_BUTTON_RIGHT_HAND_GPIO GPIO_NUM_47  // 右手触摸按键
#define TOUCH_BUTTON_LEFT_FOOT_GPIO  GPIO_NUM_48  // 左脚触摸按键
#define TOUCH_BUTTON_RIGHT_FOOT_GPIO GPIO_NUM_45  // 右脚触摸按键

// 电源管理配置
#define POWER_KEY_GPIO          GPIO_NUM_12  // KEY按键输入
#define MCU_VCC_CTL_GPIO       GPIO_NUM_4   // MCU电源控制
#define VBAT_SAMPLE_GPIO       GPIO_NUM_13  // 电池电压采样
#define AUDIO_PREP_VCC_CTL       GPIO_NUM_6   // 音频模块供电使能端

// 其他配置
#define I2C_PORT_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000

#endif // _BOARD_CONFIG_H_ 