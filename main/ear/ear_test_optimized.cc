#include "tc118s_ear_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EAR_TEST_OPTIMIZED";

// 测试优化后的耳朵控制
void test_optimized_ear_control() {
    ESP_LOGI(TAG, "=== 开始测试优化后的耳朵控制 ===");
    
    // 创建耳朵控制器实例
    auto ear_controller = new Tc118sEarController(
        (gpio_num_t)15,  // 左耳 INA
        (gpio_num_t)16,  // 左耳 INB
        (gpio_num_t)17,  // 右耳 INA
        (gpio_num_t)18   // 右耳 INB
    );
    
    if (!ear_controller) {
        ESP_LOGE(TAG, "Failed to create ear controller");
        return;
    }
    
    // 初始化耳朵控制器
    esp_err_t ret = ear_controller->Initialize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ear controller");
        delete ear_controller;
        return;
    }
    
    ESP_LOGI(TAG, "=== 测试1: 温和开心模式 (2秒) ===");
    ear_controller->PlayScenario(EAR_SCENARIO_GENTLE_HAPPY);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "=== 测试2: 惊讶模式 (1秒) ===");
    ear_controller->PlayScenario(EAR_SCENARIO_SURPRISED);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "=== 测试3: 玩耍模式 (1.8秒) ===");
    ear_controller->PlayScenario(EAR_SCENARIO_PLAYFUL);
    vTaskDelay(pdMS_TO_TICKS(2500));
    
    ESP_LOGI(TAG, "=== 测试4: 好奇模式 (1.5秒) ===");
    ear_controller->PlayScenario(EAR_SCENARIO_CURIOUS);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "=== 测试5: 兴奋模式 (2.5秒) ===");
    ear_controller->PlayScenario(EAR_SCENARIO_EXCITED);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "=== 测试6: 困倦模式 ===");
    ear_controller->PlayScenario(EAR_SCENARIO_SLEEPY);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "=== 测试7: 伤心模式 ===");
    ear_controller->PlayScenario(EAR_SCENARIO_SAD);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "=== 测试8: 情绪触发测试 ===");
    ear_controller->TriggerByEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(2500));
    
    ear_controller->TriggerByEmotion("surprised");
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    ear_controller->TriggerByEmotion("sad");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "=== 测试完成 ===");
    
    // 清理
    delete ear_controller;
}

// 测试任务
void ear_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting optimized ear control test task");
    
    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 运行测试
    test_optimized_ear_control();
    
    ESP_LOGI(TAG, "Optimized ear control test completed");
    vTaskDelete(NULL);
}
