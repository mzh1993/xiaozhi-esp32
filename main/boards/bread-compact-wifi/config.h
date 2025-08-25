#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define FAN_BUTTON_GPIO         GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_45

// 触摸按钮通道定义 - 新增玩具触摸按键
#define TOUCH_CHANNEL_HEAD      (3)   // 玩具头部触摸
#define TOUCH_CHANNEL_HAND      (9)   // 玩具手部触摸
#define TOUCH_CHANNEL_BELLY     (13)  // 玩具肚子触摸

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "未选择 OLED 屏幕类型"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

// Fan control GPIO
#define FAN_GPIO GPIO_NUM_21

// 188数码管5线动态寻址GPIO定义
// 5根控制线，既是阳极也是阴极，通过动态扫描实现矩阵寻址
#define LED188_PIN1_GPIO  GPIO_NUM_39   // 控制线1
#define LED188_PIN2_GPIO  GPIO_NUM_38   // 控制线2
#define LED188_PIN3_GPIO  GPIO_NUM_37   // 控制线3
#define LED188_PIN4_GPIO  GPIO_NUM_36   // 控制线4
#define LED188_PIN5_GPIO  GPIO_NUM_35   // 控制线5

// 注意：需要5V驱动，ESP32S3的3.3V可能不够
// 建议使用电平转换器或外部5V电源

#endif // _BOARD_CONFIG_H_
