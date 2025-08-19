#ifndef TOUCH_BUTTON_WRAPPER_H_
#define TOUCH_BUTTON_WRAPPER_H_

#include <driver/gpio.h>
#include <functional>
#include <iot_button.h>

class TouchButtonWrapper {
public:
    TouchButtonWrapper(int32_t touch_channel, float threshold = 0.15f, uint16_t long_press_time = 2000, uint16_t short_press_time = 300);
    ~TouchButtonWrapper();

    void OnPressDown(std::function<void()> callback);
    void OnPressUp(std::function<void()> callback);
    void OnLongPress(std::function<void()> callback);
    void OnClick(std::function<void()> callback);
    void OnDoubleClick(std::function<void()> callback);
    void OnMultipleClick(std::function<void()> callback, uint8_t click_count = 3);

    // 在触摸传感器初始化后创建按钮
    void CreateButton();

    // 静态方法用于初始化触摸传感器
    static void InitializeTouchSensor(const uint32_t* channel_list, int channel_count);
    static void StartTouchSensor();
    static void PreInitializeAllChannels(const uint32_t* channel_list, int channel_count);

private:
    // 静态标志，跟踪是否已经初始化
    static bool touch_sensor_initialized_;

protected:
    int32_t touch_channel_;
    button_handle_t button_handle_ = nullptr;

    std::function<void()> on_press_down_;
    std::function<void()> on_press_up_;
    std::function<void()> on_long_press_;
    std::function<void()> on_click_;
    std::function<void()> on_double_click_;
    std::function<void()> on_multiple_click_;
};

// 为了保持兼容性，提供别名
using TouchButton = TouchButtonWrapper;

#endif // TOUCH_BUTTON_WRAPPER_H_
