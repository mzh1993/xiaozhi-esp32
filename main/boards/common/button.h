#ifndef BUTTON_H_
#define BUTTON_H_

#include <driver/gpio.h>
#include <iot_button.h>
#include <functional>
#include <touch_button_sensor.h>

// ESP32 触摸传感器通道定义
#define TOUCH_PAD_GPIO6     1  // TOUCH1 
#define TOUCH_PAD_GPIO7     2  // TOUCH2
#define TOUCH_PAD_GPIO8     3  // TOUCH3
#define TOUCH_PAD_GPIO9     4  // TOUCH4
#define TOUCH_PAD_GPIO10    5  // TOUCH5
#define TOUCH_PAD_GPIO11    6  // TOUCH6
#define TOUCH_PAD_GPIO12    7  // TOUCH7
#define TOUCH_PAD_GPIO13    8  // TOUCH8
#define TOUCH_PAD_GPIO14    9  // TOUCH9
#define TOUCH_PAD_GPIO15    10 // TOUCH10
#define TOUCH_PAD_GPIO16    11 // TOUCH11
#define TOUCH_PAD_GPIO17    12 // TOUCH12
#define TOUCH_PAD_GPIO18    13 // TOUCH13
#define TOUCH_PAD_GPIO19    14 // TOUCH14

// 辅助宏，用于从GPIO编号获取触摸通道
#define GPIO_TO_TOUCH_CHANNEL(gpio_num) (((gpio_num) <= 19 && (gpio_num) >= 6) ? ((gpio_num) - 5) : -1)

// 使用宏定义创建触摸按键，代码更加清晰
// Button touch_btn3(TOUCH_CHANNEL_3, 0.2);  // 使用GPIO8的触摸传感器
// Button touch_btn9(TOUCH_CHANNEL_9, 0.2);  // 使用GPIO14的触摸传感器
// // 或者直接使用GPIO号码通过转换宏创建
// Button touch_btn_gpio15(GPIO_TO_TOUCH_CHANNEL(15), 0.2);  // 使用GPIO15 (TOUCH10)

class Button {
public:
#if CONFIG_SOC_ADC_SUPPORTED
    Button(const button_adc_config_t& cfg);
#endif
    Button(gpio_num_t gpio_num, bool active_high = false);
    Button(uint32_t touch_channel, float threshold = 0.2);
    ~Button();

    void OnPressDown(std::function<void()> callback);
    void OnPressUp(std::function<void()> callback);
    void OnLongPress(std::function<void()> callback);
    void OnClick(std::function<void()> callback);
    void OnDoubleClick(std::function<void()> callback);

private:
    gpio_num_t gpio_num_ = GPIO_NUM_NC;
    button_handle_t button_handle_ = nullptr;
    touch_button_handle_t touch_button_handle_ = nullptr;
    uint32_t touch_channel_ = UINT32_MAX;

    std::function<void()> on_press_down_;
    std::function<void()> on_press_up_;
    std::function<void()> on_long_press_;
    std::function<void()> on_click_;
    std::function<void()> on_double_click_;

    static void touch_button_callback(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void* arg);
    static void touch_event_task(void* arg);
    TaskHandle_t touch_task_handle_ = nullptr;
};

#endif // BUTTON_H_
