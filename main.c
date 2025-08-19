/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "touch_button.h"
#include "iot_button.h"
#include "touch_sensor_lowlevel.h"

static const char *TAG = "main";

#define TOUCH_CHANNEL_1        (3)
#define TOUCH_CHANNEL_2        (9)
#define TOUCH_CHANNEL_3        (13)
#define TOUCH_CHANNEL_4        (14)

#define LIGHT_TOUCH_THRESHOLD  (0.15)
#define HEAVY_TOUCH_THRESHOLD  (0.4)

static void light_button_event_cb(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "Light Button 1: %s", iot_button_get_event_str(event));
}

static void heavy_button_event_cb(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "Heavy Button 1: %s", iot_button_get_event_str(event));
}

static void touch_event_light_2(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "Light Button 2: %s", iot_button_get_event_str(event));
}

static void touch_event_light_3(void *arg, void *data)
{
    button_event_t event = iot_button_get_event(arg);
    ESP_LOGI(TAG, "Light Button 3: %s", iot_button_get_event_str(event));
}

static void touch_task(void *arg)
{
    // Register all touch channel
    uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1, TOUCH_CHANNEL_2, TOUCH_CHANNEL_3, TOUCH_CHANNEL_4};
    int total_channel_num = sizeof(touch_channel_list) / sizeof(touch_channel_list[0]);

    // calloc channel_type for every button from the list
    touch_lowlevel_type_t *channel_type = calloc(total_channel_num, sizeof(touch_lowlevel_type_t));
    assert(channel_type);
    for (int i = 0; i  < total_channel_num; i++) {
        channel_type[i] = TOUCH_LOWLEVEL_TYPE_TOUCH;
    }

    touch_lowlevel_config_t low_config = {
        .channel_num = total_channel_num,
        .channel_list = touch_channel_list,
        .channel_type = channel_type,
    };
    esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
    assert(ret == ESP_OK);
    free(channel_type);

    /* ============================= Init touch IO3 ============================= */ 
    const button_config_t btn_cfg = {
        .short_press_time = 300,
        .long_press_time = 2000,
    };
    button_touch_config_t touch_cfg_1 = {
        .touch_channel = touch_channel_list[0],
        .channel_threshold = LIGHT_TOUCH_THRESHOLD,
        .skip_lowlevel_init = true,
    };

    // Create light press button
    button_handle_t btn_light_1 = NULL;
    ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg_1, &btn_light_1);
    assert(ret == ESP_OK);

    // Create heavy press button
    touch_cfg_1.channel_threshold = HEAVY_TOUCH_THRESHOLD;
    button_handle_t btn_heavy_1 = NULL;
    ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg_1, &btn_heavy_1);
    assert(ret == ESP_OK);

    /* ============================= Init touch IO9 ============================= */ 
    button_touch_config_t touch_cfg_2 = {
        .touch_channel = touch_channel_list[1],
        .channel_threshold = LIGHT_TOUCH_THRESHOLD,
        .skip_lowlevel_init = true,
    };

    // Create light press button
    button_handle_t btn_light_2 = NULL;
    ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg_2, &btn_light_2);
    assert(ret == ESP_OK);

    /* ============================= Init touch IO13 ============================= */ 
    button_touch_config_t touch_cfg_3 = {
        .touch_channel = touch_channel_list[2],
        .channel_threshold = LIGHT_TOUCH_THRESHOLD,
        .skip_lowlevel_init = true,
    };

    // Create light press button
    button_handle_t btn_light_3 = NULL;
    ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg_3, &btn_light_3);
    assert(ret == ESP_OK);

    /* ========================== Register touch callback ========================== */ 
    // Register touch IO3 callback
    iot_button_register_cb(btn_light_1, BUTTON_PRESS_DOWN, NULL, light_button_event_cb, NULL);
    iot_button_register_cb(btn_light_1, BUTTON_PRESS_UP, NULL, light_button_event_cb, NULL);
    iot_button_register_cb(btn_heavy_1, BUTTON_PRESS_DOWN, NULL, heavy_button_event_cb, NULL);
    iot_button_register_cb(btn_heavy_1, BUTTON_PRESS_UP, NULL, heavy_button_event_cb, NULL);

    // Register touch IO9 callback
    iot_button_register_cb(btn_light_2, BUTTON_LONG_PRESS_START, NULL, touch_event_light_2, NULL);

    // Register touch IO13 callback
    iot_button_register_cb(btn_light_3, BUTTON_PRESS_DOWN, NULL, touch_event_light_3, NULL);

    touch_sensor_lowlevel_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    xTaskCreate(touch_task, "touch_task", 1024 * 5, NULL, 5, NULL);
}
