#include "ear_controller.h"
#include "tc118s_ear_controller.h"
#include "no_ear_controller.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EAR_EXAMPLE";

// 耳朵演示任务
void ear_demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear controller demo");
    
    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 创建耳朵控制器实例（使用 TC118S 实现）
    // 注意：这里使用示例 GPIO 引脚，实际使用时应该使用正确的引脚
    auto ear_controller = new Tc118sEarController(
        (gpio_num_t)15,  // 左耳 INA
        (gpio_num_t)16,  // 左耳 INB
        (gpio_num_t)17,  // 右耳 INA
        (gpio_num_t)18   // 右耳 INB
    );
    
    if (!ear_controller) {
        ESP_LOGE(TAG, "Failed to create ear controller");
        vTaskDelete(NULL);
        return;
    }
    
    // 初始化耳朵控制器
    esp_err_t ret = ear_controller->Initialize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ear controller, using NoEarController");
        delete ear_controller;
        ear_controller = new NoEarController();
        ear_controller->Initialize();
    }
    
    ESP_LOGI(TAG, "=== 开始耳朵控制器演示 ===");
    
    // 1. 基础控制演示
    ESP_LOGI(TAG, "1. 基础控制演示");
    ear_controller->SetDirection(true, EAR_FORWARD);   // 左耳向前
    ear_controller->SetDirection(false, EAR_BACKWARD); // 右耳向后
    vTaskDelay(pdMS_TO_TICKS(2000));
    ear_controller->StopBoth();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 定时移动演示
    ESP_LOGI(TAG, "2. 定时移动演示");
    ear_controller->MoveTimed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 1500);
    ear_controller->MoveTimed(false, EAR_BACKWARD, EAR_SPEED_FAST, 1000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 3. 场景模式演示
    ESP_LOGI(TAG, "3. 场景模式演示");
    
    // 躲猫猫模式
    ESP_LOGI(TAG, "   - 躲猫猫模式");
    ear_controller->PeekabooMode(3000);
    vTaskDelay(pdMS_TO_TICKS(4000));
    
    // 好奇模式
    ESP_LOGI(TAG, "   - 好奇模式");
    ear_controller->CuriousMode(2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 兴奋模式
    ESP_LOGI(TAG, "   - 兴奋模式");
    ear_controller->ExcitedMode(2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 玩耍模式
    ESP_LOGI(TAG, "   - 玩耍模式");
    ear_controller->PlayfulMode(2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 4. 情绪触发演示
    ESP_LOGI(TAG, "4. 情绪触发演示");
    
    const char* test_emotions[] = {
        "happy", "sad", "excited", "curious", "sleepy"
    };
    
    for (const char* emotion : test_emotions) {
        ESP_LOGI(TAG, "   触发情绪: %s", emotion);
        ear_controller->TriggerByEmotion(emotion);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    // 5. 自定义模式演示
    ESP_LOGI(TAG, "5. 自定义模式演示");
    
    ear_movement_step_t custom_steps[] = {
        {EAR_FORWARD, EAR_SPEED_NORMAL, 500, 200},
        {EAR_BACKWARD, EAR_SPEED_FAST, 300, 100},
        {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 50},
        {EAR_BACKWARD, EAR_SPEED_NORMAL, 400, 150}
    };
    
    ear_controller->PlayCustomPattern(custom_steps, 4, true);
    vTaskDelay(pdMS_TO_TICKS(5000));
    ear_controller->StopScenario();
    
    ESP_LOGI(TAG, "=== 耳朵控制器演示完成 ===");
    
    // 清理
    ear_controller->StopBoth();
    
    vTaskDelete(NULL);
}

// 触摸响应演示任务
void ear_touch_response_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear touch response demo");
    
    // 创建耳朵控制器实例
    auto ear_controller = new NoEarController();
    ear_controller->Initialize();
    
    if (!ear_controller) {
        ESP_LOGE(TAG, "No ear controller available for touch response");
        vTaskDelete(NULL);
        return;
    }
    
    // 模拟触摸事件响应
    while (1) {
        // 这里可以集成真实的触摸事件
        ESP_LOGI(TAG, "模拟触摸事件 - 头部触摸");
        ear_controller->TriggerByEmotion("happy");
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        ESP_LOGI(TAG, "模拟触摸事件 - 手部触摸");
        ear_controller->TriggerByEmotion("curious");
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        ESP_LOGI(TAG, "模拟触摸事件 - 腹部触摸");
        ear_controller->TriggerByEmotion("playful");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// 情绪演示任务
void ear_emotion_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear emotion demo");
    
    // 创建耳朵控制器实例
    auto ear_controller = new NoEarController();
    ear_controller->Initialize();
    
    if (!ear_controller) {
        ESP_LOGE(TAG, "No ear controller available for emotion demo");
        vTaskDelete(NULL);
        return;
    }
    
    // 情绪转换演示
    while (1) {
        ESP_LOGI(TAG, "情绪转换: neutral -> happy");
        ear_controller->TransitionEmotion("neutral", "happy", 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "情绪转换: happy -> sad");
        ear_controller->TransitionEmotion("happy", "sad", 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "情绪转换: sad -> excited");
        ear_controller->TransitionEmotion("sad", "excited", 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "情绪转换: excited -> sleepy");
        ear_controller->TransitionEmotion("excited", "sleepy", 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "情绪转换: sleepy -> neutral");
        ear_controller->TransitionEmotion("sleepy", "neutral", 1000);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// 初始化耳朵示例
void ear_example_init() {
    ESP_LOGI(TAG, "Initializing ear controller examples");
    
    // 创建演示任务
    xTaskCreate(ear_demo_task, "ear_demo", 4096, NULL, 5, NULL);
    
    // 创建触摸响应任务
    xTaskCreate(ear_touch_response_task, "ear_touch", 4096, NULL, 4, NULL);
    
    // 创建情绪演示任务
    xTaskCreate(ear_emotion_task, "ear_emotion", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "Ear controller examples initialized");
}
