#include "ear_emotion_integration.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EAR_EMOTION_TEST";

// 测试任务：模拟LLM情绪消息
void ear_emotion_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear emotion integration test");
    
    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 测试所有支持的情绪
    const char* test_emotions[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry", 
        "crying", "loving", "embarrassed", "surprised", "shocked",
        "thinking", "winking", "cool", "relaxed", "delicious",
        "kissy", "confident", "sleepy", "silly", "confused"
    };
    
    while (1) {
        ESP_LOGI(TAG, "=== 开始耳朵情绪集成测试 ===");
        
        for (int i = 0; i < sizeof(test_emotions)/sizeof(test_emotions[0]); i++) {
            ESP_LOGI(TAG, "测试情绪: %s", test_emotions[i]);
            
            // 模拟LLM情绪消息
            ear_trigger_by_emotion(test_emotions[i]);
            
            // 等待动作完成
            vTaskDelay(pdMS_TO_TICKS(4000));
            
            // 短暂停止
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        ESP_LOGI(TAG, "=== 测试完成，等待下一轮 ===");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// 测试自定义情绪映射
void ear_custom_mapping_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting custom emotion mapping test");
    
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    while (1) {
        ESP_LOGI(TAG, "=== 自定义情绪映射测试 ===");
        
        // 设置自定义映射
        ear_set_emotion_mapping("happy", EAR_SCENARIO_EXCITED, 5000);
        ear_set_emotion_mapping("sad", EAR_SCENARIO_PEEKABOO, 3000);  // 反常规映射
        
        // 测试自定义映射
        ESP_LOGI(TAG, "测试自定义happy映射（兴奋模式5秒）");
        ear_trigger_by_emotion("happy");
        vTaskDelay(pdMS_TO_TICKS(6000));
        
        ESP_LOGI(TAG, "测试自定义sad映射（躲猫猫模式3秒）");
        ear_trigger_by_emotion("sad");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 恢复默认映射
        ear_set_emotion_mapping("happy", EAR_SCENARIO_PLAYFUL, 3000);
        ear_set_emotion_mapping("sad", EAR_SCENARIO_SAD, 0);
        
        ESP_LOGI(TAG, "=== 自定义映射测试完成 ===");
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

// 测试情绪强度功能
void ear_intensity_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting emotion intensity test");
    
    vTaskDelay(pdMS_TO_TICKS(15000));
    
    while (1) {
        ESP_LOGI(TAG, "=== 情绪强度测试 ===");
        
        // 测试不同强度的happy情绪
        ESP_LOGI(TAG, "测试低强度happy (0.3)");
        ear_trigger_by_emotion_with_intensity("happy", 0.3f);
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        ESP_LOGI(TAG, "测试中等强度happy (0.6)");
        ear_trigger_by_emotion_with_intensity("happy", 0.6f);
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        ESP_LOGI(TAG, "测试高强度happy (0.9)");
        ear_trigger_by_emotion_with_intensity("happy", 0.9f);
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 测试情绪转换
        ESP_LOGI(TAG, "测试情绪转换: happy -> sad");
        ear_transition_emotion("happy", "sad", 2000);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ESP_LOGI(TAG, "测试情绪转换: sad -> excited");
        ear_transition_emotion("sad", "excited", 2000);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ESP_LOGI(TAG, "=== 强度测试完成 ===");
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}

// 模拟真实LLM消息的测试
void ear_llm_simulation_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting LLM message simulation test");
    
    vTaskDelay(pdMS_TO_TICKS(20000));
    
    while (1) {
        ESP_LOGI(TAG, "=== LLM消息模拟测试 ===");
        
        // 模拟LLM返回的JSON消息
        // 这些消息会通过Application::OnIncomingJson处理
        
        // 模拟用户说笑话，LLM返回happy情绪
        ESP_LOGI(TAG, "模拟: 用户讲笑话 -> LLM返回happy情绪");
        ear_trigger_by_emotion("happy");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 模拟用户说伤心事，LLM返回sad情绪
        ESP_LOGI(TAG, "模拟: 用户说伤心事 -> LLM返回sad情绪");
        ear_trigger_by_emotion("sad");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 模拟用户说惊讶的事，LLM返回surprised情绪
        ESP_LOGI(TAG, "模拟: 用户说惊讶的事 -> LLM返回surprised情绪");
        ear_trigger_by_emotion("surprised");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 模拟用户说困倦，LLM返回sleepy情绪
        ESP_LOGI(TAG, "模拟: 用户说困倦 -> LLM返回sleepy情绪");
        ear_trigger_by_emotion("sleepy");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 模拟用户说兴奋的事，LLM返回excited情绪
        ESP_LOGI(TAG, "模拟: 用户说兴奋的事 -> LLM返回excited情绪");
        ear_trigger_by_emotion("excited");
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ESP_LOGI(TAG, "=== LLM模拟测试完成 ===");
        vTaskDelay(pdMS_TO_TICKS(25000));
    }
}

// 初始化测试
esp_err_t ear_emotion_test_init(void) {
    ESP_LOGI(TAG, "Initializing ear emotion integration tests");
    
    // 创建测试任务
    xTaskCreate(ear_emotion_test_task, "ear_emotion_test", 4096, NULL, 3, NULL);
    xTaskCreate(ear_custom_mapping_test_task, "ear_custom_test", 4096, NULL, 2, NULL);
    xTaskCreate(ear_intensity_test_task, "ear_intensity_test", 4096, NULL, 2, NULL);
    xTaskCreate(ear_llm_simulation_task, "ear_llm_sim_test", 4096, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Ear emotion integration tests initialized successfully");
    return ESP_OK;
}
