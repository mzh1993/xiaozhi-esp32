#include "button.h"

#include <esp_log.h>

static const char* TAG = "Button";
#if CONFIG_SOC_ADC_SUPPORTED
Button::Button(const button_adc_config_t& adc_cfg) {
    button_config_t button_config = {
        .type = BUTTON_TYPE_ADC,
        .long_press_time = 1000,
        .short_press_time = 50,
        .adc_button_config = adc_cfg
    };
    button_handle_ = iot_button_create(&button_config);
    if (button_handle_ == NULL) {
        ESP_LOGE(TAG, "Failed to create button handle");
        return;
    }
}
#endif

Button::Button(gpio_num_t gpio_num, bool active_high) : gpio_num_(gpio_num) {
    if (gpio_num == GPIO_NUM_NC) {
        return;
    }
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = gpio_num,
            .active_level = static_cast<uint8_t>(active_high ? 1 : 0)
        }
    };
    button_handle_ = iot_button_create(&button_config);
    if (button_handle_ == NULL) {
        ESP_LOGE(TAG, "Failed to create button handle");
        return;
    }
}

Button::Button(uint32_t touch_channel, float threshold) : touch_channel_(touch_channel) {
    ESP_LOGI(TAG, "Creating touch button for channel %lu with threshold %.2f", touch_channel, threshold);
    
    touch_button_config_t config = {
        .channel_num = 1,
        .channel_list = &touch_channel_,
        .channel_threshold = &threshold,
        .channel_gold_value = NULL,
        .debounce_times = 3,
        .skip_lowlevel_init = false
    };
    
    esp_err_t ret = touch_button_sensor_create(&config, &touch_button_handle_, touch_button_callback, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch button: %s", esp_err_to_name(ret));
        return;
    }
    
    xTaskCreate(touch_event_task, "touch_task", 4096, this, 5, &touch_task_handle_);
    
    ESP_LOGI(TAG, "Touch button created successfully");
}

void Button::touch_event_task(void* arg) {
    Button* button = static_cast<Button*>(arg);
    
    while (1) {
        if (button->touch_button_handle_) {
            touch_button_sensor_handle_events(button->touch_button_handle_);
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

Button::~Button() {
    if (button_handle_ != NULL) {
        iot_button_delete(button_handle_);
    }
    
    if (touch_button_handle_ != NULL) {
        touch_button_sensor_delete(touch_button_handle_);
        
        if (touch_task_handle_ != NULL) {
            vTaskDelete(touch_task_handle_);
            touch_task_handle_ = NULL;
        }
    }
}

void Button::touch_button_callback(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void* arg) {
    Button* button = static_cast<Button*>(arg);
    if (!button) {
        return;
    }
    
    if (state == TOUCH_STATE_ACTIVE) {
        if (button->on_press_down_) {
            button->on_press_down_();
        }
    } else if (state == TOUCH_STATE_INACTIVE) {
        if (button->on_press_up_) {
            button->on_press_up_();
        }
        
        if (button->on_click_) {
            button->on_click_();
        }
    }
}

void Button::OnPressDown(std::function<void()> callback) {
    if (button_handle_ == nullptr && touch_button_handle_ == nullptr) {
        return;
    }
    on_press_down_ = callback;
    
    if (button_handle_) {
        iot_button_register_cb(button_handle_, BUTTON_PRESS_DOWN, [](void* handle, void* usr_data) {
            Button* button = static_cast<Button*>(usr_data);
            if (button->on_press_down_) {
                button->on_press_down_();
            }
        }, this);
    }
}

void Button::OnPressUp(std::function<void()> callback) {
    if (button_handle_ == nullptr && touch_button_handle_ == nullptr) {
        return;
    }
    on_press_up_ = callback;
    
    if (button_handle_) {
        iot_button_register_cb(button_handle_, BUTTON_PRESS_UP, [](void* handle, void* usr_data) {
            Button* button = static_cast<Button*>(usr_data);
            if (button->on_press_up_) {
                button->on_press_up_();
            }
        }, this);
    }
}

void Button::OnLongPress(std::function<void()> callback) {
    if (button_handle_ == nullptr && touch_button_handle_ == nullptr) {
        return;
    }
    on_long_press_ = callback;
    
    if (button_handle_) {
        iot_button_register_cb(button_handle_, BUTTON_LONG_PRESS_START, [](void* handle, void* usr_data) {
            Button* button = static_cast<Button*>(usr_data);
            if (button->on_long_press_) {
                button->on_long_press_();
            }
        }, this);
    }
}

void Button::OnClick(std::function<void()> callback) {
    if (button_handle_ == nullptr && touch_button_handle_ == nullptr) {
        return;
    }
    on_click_ = callback;
    
    if (button_handle_) {
        iot_button_register_cb(button_handle_, BUTTON_SINGLE_CLICK, [](void* handle, void* usr_data) {
            Button* button = static_cast<Button*>(usr_data);
            if (button->on_click_) {
                button->on_click_();
            }
        }, this);
    }
}

void Button::OnDoubleClick(std::function<void()> callback) {
    if (button_handle_ == nullptr && touch_button_handle_ == nullptr) {
        return;
    }
    on_double_click_ = callback;
    
    if (button_handle_) {
        iot_button_register_cb(button_handle_, BUTTON_DOUBLE_CLICK, [](void* handle, void* usr_data) {
            Button* button = static_cast<Button*>(usr_data);
            if (button->on_double_click_) {
                button->on_double_click_();
            }
        }, this);
    }
}
