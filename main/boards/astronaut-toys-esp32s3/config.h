#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Astronaut toys configuration

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_6
#define AUDIO_I2S_GPIO_WS GPIO_NUM_12
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_14
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_13
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_11

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_40
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_5
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_4
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_9
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_45
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_8
#define KEY1_BUTTON_GPIO GPIO_NUM_47
#define KEY2_BUTTON_GPIO GPIO_NUM_48

// 触摸按钮通道定义 - 新增玩具触摸按键（避免与现有功能冲突）
#define TOUCH_CHANNEL_HEAD      (1) 
#define TOUCH_CHANNEL_HAND      (2)
#define TOUCH_CHANNEL_BELLY     (3) 

#define LAMP_GPIO GPIO_NUM_18
#define FAN_GPIO GPIO_NUM_21

#define VBAT_ADC_CHANNEL         ADC_CHANNEL_9 
#define ADC_ATTEN                ADC_ATTEN_DB_12
#define ADC_WIDTH                ADC_BITWIDTH_DEFAULT
#define FULL_BATTERY_VOLTAGE     4100
#define EMPTY_BATTERY_VOLTAGE    3200

// 添加 OLED 屏幕配置
#define DISPLAY_SDA_PIN AUDIO_CODEC_I2C_SDA_PIN
#define DISPLAY_SCL_PIN AUDIO_CODEC_I2C_SCL_PIN
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

// #define BOARD_NAME "astronaut-toys-esp32s3"

// Ear Motor Control Pins for TC118S
#define LEFT_EAR_INA_GPIO   GPIO_NUM_15   // 左耳朵电机控制引脚A
#define LEFT_EAR_INB_GPIO   GPIO_NUM_16   // 左耳朵电机控制引脚B
#define RIGHT_EAR_INA_GPIO  GPIO_NUM_17   // 右耳朵电机控制引脚A
#define RIGHT_EAR_INB_GPIO  GPIO_NUM_18   // 右耳朵电机控制引脚B

// Ear Movement Patterns
typedef enum {
    EAR_STOP = 0,           // 停止
    EAR_FORWARD = 1,        // 向前摆动
    EAR_BACKWARD = 2,       // 向后摆动
    EAR_BRAKE = 3           // 刹车
} ear_direction_t;

typedef enum {
    EAR_SPEED_SLOW = 1,     // 慢速
    EAR_SPEED_NORMAL = 2,   // 正常速度
    EAR_SPEED_FAST = 3,     // 快速
    EAR_SPEED_VERY_FAST = 4 // 极快速度
} ear_speed_t;

// Ear Movement Scenarios
typedef enum {
    EAR_SCENARIO_NORMAL = 0,        // 正常状态
    EAR_SCENARIO_PEEKABOO = 1,      // 躲猫猫 - 双耳长时间向前
    EAR_SCENARIO_INSECT_BITE = 2,   // 蚊虫叮咬 - 单边快速摆动
    EAR_SCENARIO_CURIOUS = 3,       // 好奇 - 双耳交替摆动
    EAR_SCENARIO_SLEEPY = 4,        // 困倦 - 缓慢下垂
    EAR_SCENARIO_EXCITED = 5,       // 兴奋 - 快速摆动
    EAR_SCENARIO_SAD = 6,           // 伤心 - 耳朵下垂
    EAR_SCENARIO_ALERT = 7,         // 警觉 - 耳朵竖起
    EAR_SCENARIO_PLAYFUL = 8,       // 玩耍 - 不规则摆动
    EAR_SCENARIO_CUSTOM = 9         // 自定义模式
} ear_scenario_t;

#endif // _BOARD_CONFIG_H_
