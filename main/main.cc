#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    // esp_event_loop_create_default() 创建默认事件循环
    // 这是ESP-IDF系统中必须的初始化步骤之一
    // 它创建一个事件循环来处理系统事件(如WiFi、定时器等)
    // 必须在使用任何需要事件通知的组件之前调用
    // ESP_ERROR_CHECK is a macro defined in esp-idf/components/esp_common/include/esp_err.h
    // 它检查返回值是否为ESP_OK(0),否则记录错误并中止
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    // 初始化NVS flash用于WiFi配置
    // 这是ESP-IDF系统中必须的初始化步骤之一
    // 它初始化NVS flash存储器,用于存储WiFi配置信息
    // 如果NVS flash存储器损坏,它会擦除并重新初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 如果NVS flash存储器损坏,它会擦除并重新初始化
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        // 擦除NVS flash存储器
        ESP_ERROR_CHECK(nvs_flash_erase());
        // 重新初始化NVS flash
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Launch the application
    // 启动应用程序
    // 这是应用程序的入口点

    
    // 它创建一个应用程序实例并开始执行
    Application::GetInstance().Start();
}
