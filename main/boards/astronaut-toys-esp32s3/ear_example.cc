#include "ear_controller.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EAR_EXAMPLE";

// Example task demonstrating ear movements
void ear_demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear demo task");
    
    // Initialize ear controller
    esp_err_t ret = ear_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ear controller");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        ESP_LOGI(TAG, "=== 耳朵拟人化演示开始 ===");
        
        // 1. 躲猫猫模式 - 双耳长时间向前盖住眼睛
        ESP_LOGI(TAG, "1. 躲猫猫模式 - 双耳向前盖住眼睛");
        ear_peekaboo_mode(8000);  // 8秒
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 2. 蚊虫叮咬模式 - 左耳快速摆动
        ESP_LOGI(TAG, "2. 蚊虫叮咬模式 - 左耳快速摆动");
        ear_insect_bite_mode(true, 3000);  // 左耳，3秒
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 3. 好奇模式 - 双耳交替摆动
        ESP_LOGI(TAG, "3. 好奇模式 - 双耳交替摆动");
        ear_curious_mode(5000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 4. 困倦模式 - 耳朵缓慢下垂
        ESP_LOGI(TAG, "4. 困倦模式 - 耳朵缓慢下垂");
        ear_sleepy_mode();
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 5. 兴奋模式 - 快速摆动
        ESP_LOGI(TAG, "5. 兴奋模式 - 快速摆动");
        ear_excited_mode(4000);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 6. 伤心模式 - 耳朵下垂
        ESP_LOGI(TAG, "6. 伤心模式 - 耳朵下垂");
        ear_sad_mode();
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 7. 警觉模式 - 耳朵竖起
        ESP_LOGI(TAG, "7. 警觉模式 - 耳朵竖起");
        ear_alert_mode();
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 8. 玩耍模式 - 不规则摆动
        ESP_LOGI(TAG, "8. 玩耍模式 - 不规则摆动");
        ear_playful_mode(6000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // 9. 使用场景模式
        ESP_LOGI(TAG, "9. 使用场景模式");
        ear_play_scenario(EAR_SCENARIO_PEEKABOO);
        vTaskDelay(pdMS_TO_TICKS(6000));
        
        ear_play_scenario(EAR_SCENARIO_INSECT_BITE);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ear_play_scenario(EAR_SCENARIO_CURIOUS);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ear_play_scenario(EAR_SCENARIO_EXCITED);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        ear_play_scenario(EAR_SCENARIO_PLAYFUL);
        vTaskDelay(pdMS_TO_TICKS(4000));
        
        // 10. 自定义模式演示
        ESP_LOGI(TAG, "10. 自定义模式演示");
        
        // 创建自定义摆动模式
        ear_movement_step_t custom_steps[] = {
            {EAR_FORWARD, EAR_SPEED_SLOW, 1000, 500},      // 慢速向前1秒
            {EAR_BACKWARD, EAR_SPEED_FAST, 500, 200},      // 快速向后0.5秒
            {EAR_FORWARD, EAR_SPEED_VERY_FAST, 300, 100},  // 极快向前0.3秒
            {EAR_BACKWARD, EAR_SPEED_NORMAL, 800, 400},    // 正常速度向后0.8秒
            {EAR_FORWARD, EAR_SPEED_SLOW, 1500, 1000}     // 慢速向前1.5秒
        };
        
        ear_scenario_config_t custom_config = {
            .scenario = EAR_SCENARIO_CUSTOM,
            .steps = custom_steps,
            .step_count = 5,
            .loop_enabled = true,
            .loop_count = 2
        };
        
        ear_set_custom_scenario(&custom_config);
        ear_play_scenario(EAR_SCENARIO_CUSTOM);
        vTaskDelay(pdMS_TO_TICKS(8000));
        
        ESP_LOGI(TAG, "=== 耳朵拟人化演示完成，等待下一轮 ===");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// 高级使用示例：根据触摸事件触发耳朵动作
void ear_touch_response_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear touch response task");
    
    while (1) {
        // 模拟触摸检测事件
        // 这里可以集成实际的触摸检测逻辑
        
        // 根据不同的触摸位置触发不同的耳朵动作
        // 例如：触摸肚子 -> 兴奋摆动
        //      触摸头部 -> 好奇摆动
        //      触摸耳朵 -> 蚊虫叮咬模式
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 情绪状态管理示例
typedef enum {
    EMOTION_NEUTRAL = 0,
    EMOTION_HAPPY,
    EMOTION_SAD,
    EMOTION_EXCITED,
    EMOTION_SLEEPY,
    EMOTION_CURIOUS,
    EMOTION_ALERT
} emotion_state_t;

static emotion_state_t current_emotion = EMOTION_NEUTRAL;

void ear_emotion_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting ear emotion task");
    
    while (1) {
        // 根据当前情绪状态控制耳朵
        switch (current_emotion) {
            case EMOTION_HAPPY:
                ear_playful_mode(3000);
                break;
            case EMOTION_SAD:
                ear_sad_mode();
                break;
            case EMOTION_EXCITED:
                ear_excited_mode(4000);
                break;
            case EMOTION_SLEEPY:
                ear_sleepy_mode();
                break;
            case EMOTION_CURIOUS:
                ear_curious_mode(3000);
                break;
            case EMOTION_ALERT:
                ear_alert_mode();
                break;
            default:
                // 正常状态，耳朵保持静止
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// 初始化函数
esp_err_t ear_example_init(void) {
    ESP_LOGI(TAG, "Initializing ear example");
    
    // 创建演示任务
    xTaskCreate(ear_demo_task, "ear_demo", 4096, NULL, 5, NULL);
    
    // 创建触摸响应任务
    xTaskCreate(ear_touch_response_task, "ear_touch", 2048, NULL, 4, NULL);
    
    // 创建情绪管理任务
    xTaskCreate(ear_emotion_task, "ear_emotion", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "Ear example initialized successfully");
    return ESP_OK;
}

// 设置情绪状态
void ear_set_emotion(emotion_state_t emotion) {
    current_emotion = emotion;
    ESP_LOGI(TAG, "Emotion changed to: %d", emotion);
}

// 获取当前情绪状态
emotion_state_t ear_get_emotion(void) {
    return current_emotion;
}
