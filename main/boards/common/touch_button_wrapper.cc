#include "touch_button_wrapper.h"
#include <esp_log.h>

// 包含必要的头文件
extern "C" {
    #include "iot_button.h"
    #include "button_types.h"
    #include "touch_button.h"
    #include "touch_button_sensor.h"
    #include "touch_sensor_lowlevel.h"
}

#define TAG "TouchButtonWrapper"

// 静态变量定义
bool TouchButtonWrapper::touch_sensor_initialized_ = false;

TouchButtonWrapper::TouchButtonWrapper(int32_t touch_channel, float threshold, uint16_t long_press_time, uint16_t short_press_time) 
    : touch_channel_(touch_channel), threshold_(threshold), long_press_time_(long_press_time), short_press_time_(short_press_time) {
    
    if (touch_channel < 0) {
        return;
    }

    // 检查触摸传感器是否已初始化
    if (!touch_sensor_initialized_) {
        ESP_LOGW(TAG, "Touch sensor not initialized yet for channel %ld, button creation will be delayed", touch_channel);
        button_handle_ = nullptr;
        return;  // 暂时不创建按钮，等待后续初始化
    }

    // 如果已初始化，直接创建按钮
    CreateButton();
}

void TouchButtonWrapper::CreateButton() {
    if (button_handle_ != nullptr) {
        ESP_LOGI(TAG, "Button for channel %ld already created", touch_channel_);
        return;
    }

    if (!touch_sensor_initialized_) {
        ESP_LOGE(TAG, "Touch sensor not initialized, cannot create button for channel %ld", touch_channel_);
        return;
    }

    button_config_t btn_config = {};
    btn_config.short_press_time = short_press_time_;
    btn_config.long_press_time = long_press_time_;

    button_touch_config_t touch_config = {};
    touch_config.touch_channel = touch_channel_;
    touch_config.channel_threshold = threshold_;
    touch_config.skip_lowlevel_init = true;

    ESP_LOGI(TAG, "Creating touch button - Channel: %ld, Threshold: %.2f, SkipInit: true", 
             touch_channel_, threshold_);

    esp_err_t ret = iot_button_new_touch_button_device(&btn_config, &touch_config, &button_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch button for channel %ld, error: %s", touch_channel_, esp_err_to_name(ret));
        button_handle_ = nullptr;
    } else {
        ESP_LOGI(TAG, "Touch button created successfully for channel %ld with threshold %.2f", touch_channel_, threshold_);
    }
}

TouchButtonWrapper::~TouchButtonWrapper() {
    if (button_handle_ != nullptr) {
        iot_button_delete(button_handle_);
    }
}

void TouchButtonWrapper::OnPressDown(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_press_down_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_PRESS_DOWN, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_press_down_) {
            button->on_press_down_();
        }
    }, this);
}

void TouchButtonWrapper::OnPressUp(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_press_up_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_press_up_) {
            button->on_press_up_();
        }
    }, this);
}

void TouchButtonWrapper::OnLongPress(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_long_press_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_long_press_) {
            button->on_long_press_();
        }
    }, this);
}

void TouchButtonWrapper::OnClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_click_) {
            button->on_click_();
        }
    }, this);
}

void TouchButtonWrapper::OnDoubleClick(std::function<void()> callback) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_double_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_double_click_) {
            button->on_double_click_();
        }
    }, this);
}

void TouchButtonWrapper::OnMultipleClick(std::function<void()> callback, uint8_t click_count) {
    if (button_handle_ == nullptr) {
        return;
    }
    on_multiple_click_ = callback;
    iot_button_register_cb(button_handle_, BUTTON_MULTIPLE_CLICK, nullptr, [](void* handle, void* usr_data) {
        TouchButtonWrapper* button = static_cast<TouchButtonWrapper*>(usr_data);
        if (button->on_multiple_click_) {
            button->on_multiple_click_();
        }
    }, this);
}

void TouchButtonWrapper::InitializeTouchSensor(const uint32_t* channel_list, int channel_count) {
    if (touch_sensor_initialized_) {
        ESP_LOGI(TAG, "Touch sensor already initialized, skipping");
        return;
    }

    if (!channel_list || channel_count <= 0) {
        ESP_LOGE(TAG, "Invalid channel list or count");
        return;
    }

    ESP_LOGI(TAG, "Initializing touch sensor lowlevel system for %d channels", channel_count);
    
    // 为每个通道分配类型
    touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(channel_count, sizeof(touch_lowlevel_type_t));
    if (!channel_type) {
        ESP_LOGE(TAG, "Failed to allocate memory for channel types");
        return;
    }
    
    for (int i = 0; i < channel_count; i++) {
        channel_type[i] = TOUCH_LOWLEVEL_TYPE_TOUCH;
        ESP_LOGI(TAG, "Configuring touch channel %d", (int)channel_list[i]);
    }

    touch_lowlevel_config_t low_config = {
        .channel_num = (uint32_t)channel_count,  // 显式转换为 uint32_t
        .channel_list = (uint32_t*)channel_list,  // 临时转换，因为API需要非const指针
        .channel_type = channel_type,
    };
    
    esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
    free(channel_type);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch sensor lowlevel system initialized successfully with %d channels", channel_count);
        touch_sensor_initialized_ = true;
    } else {
        ESP_LOGE(TAG, "Failed to initialize touch sensor lowlevel system: %s", esp_err_to_name(ret));
    }
}

void TouchButtonWrapper::StartTouchSensor() {
    if (!touch_sensor_initialized_) {
        ESP_LOGE(TAG, "Touch sensor not initialized, cannot start");
        return;
    }
    
    ESP_LOGI(TAG, "Starting touch sensor lowlevel system");
    touch_sensor_lowlevel_start();
    ESP_LOGI(TAG, "Touch sensor lowlevel system started");
}
