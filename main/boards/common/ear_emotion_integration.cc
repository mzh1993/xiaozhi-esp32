#include "ear_emotion_integration.h"
#include "ear_controller.h"
#include "esp_log.h"
#include <string>
#include <map>

static const char *TAG = "EAR_EMOTION_INTEGRATION";

// 全局变量
static std::map<std::string, emotion_ear_mapping_t> emotion_mappings;
static bool integration_initialized = false;

// 默认情绪映射表
static const std::map<std::string, emotion_ear_mapping_t> default_emotion_mappings = {
    {"neutral", {EAR_SCENARIO_NORMAL, 0, true}},
    {"happy", {EAR_SCENARIO_PLAYFUL, 3000, true}},
    {"laughing", {EAR_SCENARIO_EXCITED, 4000, true}},
    {"funny", {EAR_SCENARIO_PLAYFUL, 2500, true}},
    {"sad", {EAR_SCENARIO_SAD, 0, false}},  // 伤心时耳朵下垂，不自动停止
    {"angry", {EAR_SCENARIO_ALERT, 2000, true}},
    {"crying", {EAR_SCENARIO_SAD, 0, false}},  // 哭泣时耳朵下垂
    {"loving", {EAR_SCENARIO_CURIOUS, 2000, true}},
    {"embarrassed", {EAR_SCENARIO_SAD, 1500, true}},
    {"surprised", {EAR_SCENARIO_ALERT, 1000, true}},
    {"shocked", {EAR_SCENARIO_ALERT, 1500, true}},
    {"thinking", {EAR_SCENARIO_CURIOUS, 3000, true}},
    {"winking", {EAR_SCENARIO_PLAYFUL, 1500, true}},
    {"cool", {EAR_SCENARIO_ALERT, 1000, true}},
    {"relaxed", {EAR_SCENARIO_NORMAL, 0, true}},
    {"delicious", {EAR_SCENARIO_EXCITED, 2000, true}},
    {"kissy", {EAR_SCENARIO_CURIOUS, 1500, true}},
    {"confident", {EAR_SCENARIO_ALERT, 1000, true}},
    {"sleepy", {EAR_SCENARIO_SLEEPY, 0, false}},  // 困倦时耳朵下垂
    {"silly", {EAR_SCENARIO_PLAYFUL, 3000, true}},
    {"confused", {EAR_SCENARIO_CURIOUS, 2500, true}}
};

// 初始化情绪集成系统
esp_err_t ear_emotion_integration_init(void) {
    ESP_LOGI(TAG, "Initializing ear emotion integration");
    
    // 初始化耳朵控制器
    esp_err_t ret = ear_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ear controller");
        return ret;
    }
    
    // 复制默认映射到全局映射表
    emotion_mappings = default_emotion_mappings;
    
    integration_initialized = true;
    ESP_LOGI(TAG, "Ear emotion integration initialized successfully");
    return ESP_OK;
}

// 反初始化情绪集成系统
esp_err_t ear_emotion_integration_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing ear emotion integration");
    
    if (integration_initialized) {
        ear_controller_deinit();
        emotion_mappings.clear();
        integration_initialized = false;
    }
    
    ESP_LOGI(TAG, "Ear emotion integration deinitialized");
    return ESP_OK;
}

// 根据情绪字符串触发对应的耳朵动作
esp_err_t ear_trigger_by_emotion(const char* emotion) {
    if (!integration_initialized || emotion == nullptr) {
        ESP_LOGW(TAG, "Integration not initialized or invalid emotion");
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings.find(emotion_str);
    
    if (it == emotion_mappings.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s, using neutral", emotion);
        // 未知情绪使用neutral
        it = emotion_mappings.find("neutral");
        if (it == emotion_mappings.end()) {
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    const emotion_ear_mapping_t& mapping = it->second;
    ESP_LOGI(TAG, "Triggering ear action for emotion: %s, scenario: %d, duration: %d ms", 
             emotion, mapping.ear_scenario, mapping.duration_ms);
    
    // 根据场景类型执行不同的耳朵动作
    switch (mapping.ear_scenario) {
        case EAR_SCENARIO_NORMAL:
            // 正常状态，停止所有耳朵动作
            ear_stop_both();
            break;
            
        case EAR_SCENARIO_PEEKABOO:
            ear_peekaboo_mode(mapping.duration_ms);
            break;
            
        case EAR_SCENARIO_INSECT_BITE:
            // 蚊虫叮咬模式，随机选择左耳或右耳
            ear_insect_bite_mode((rand() % 2) == 0, mapping.duration_ms);
            break;
            
        case EAR_SCENARIO_CURIOUS:
            ear_curious_mode(mapping.duration_ms);
            break;
            
        case EAR_SCENARIO_SLEEPY:
            ear_sleepy_mode();
            break;
            
        case EAR_SCENARIO_EXCITED:
            ear_excited_mode(mapping.duration_ms);
            break;
            
        case EAR_SCENARIO_SAD:
            ear_sad_mode();
            break;
            
        case EAR_SCENARIO_ALERT:
            ear_alert_mode();
            break;
            
        case EAR_SCENARIO_PLAYFUL:
            ear_playful_mode(mapping.duration_ms);
            break;
            
        case EAR_SCENARIO_CUSTOM:
            // 自定义场景，这里可以扩展
            ESP_LOGW(TAG, "Custom scenario not implemented yet");
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown ear scenario: %d", mapping.ear_scenario);
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

// 设置自定义情绪映射
esp_err_t ear_set_emotion_mapping(const char* emotion, ear_scenario_t scenario, uint32_t duration_ms) {
    if (!integration_initialized || emotion == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string emotion_str(emotion);
    emotion_ear_mapping_t mapping = {
        .ear_scenario = scenario,
        .duration_ms = duration_ms,
        .auto_stop = true
    };
    
    emotion_mappings[emotion_str] = mapping;
    ESP_LOGI(TAG, "Set custom emotion mapping: %s -> scenario %d, duration %d ms", 
             emotion, scenario, duration_ms);
    
    return ESP_OK;
}

// 获取当前情绪对应的耳朵动作
emotion_ear_mapping_t* ear_get_emotion_mapping(const char* emotion) {
    if (!integration_initialized || emotion == nullptr) {
        return nullptr;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings.find(emotion_str);
    
    if (it != emotion_mappings.end()) {
        return &(it->second);
    }
    
    return nullptr;
}

// 停止当前情绪相关的耳朵动作
esp_err_t ear_stop_emotion_action(void) {
    if (!integration_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping current emotion-related ear action");
    ear_stop_both();
    return ESP_OK;
}

// 高级功能：根据情绪强度调整耳朵动作
esp_err_t ear_trigger_by_emotion_with_intensity(const char* emotion, float intensity) {
    if (!integration_initialized || emotion == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 根据强度调整持续时间
    uint32_t base_duration = 2000;  // 基础持续时间2秒
    uint32_t adjusted_duration = (uint32_t)(base_duration * intensity);
    
    // 根据强度调整速度
    ear_speed_t speed = EAR_SPEED_NORMAL;
    if (intensity > 0.8f) {
        speed = EAR_SPEED_FAST;
    } else if (intensity < 0.3f) {
        speed = EAR_SPEED_SLOW;
    }
    
    ESP_LOGI(TAG, "Triggering ear action with intensity: %s, intensity: %.2f, duration: %d ms", 
             emotion, intensity, adjusted_duration);
    
    // 这里可以根据情绪和强度组合实现更复杂的动作
    // 例如：高强度的happy -> 更快的玩耍模式
    //      低强度的sad -> 更慢的下垂动作
    
    return ear_trigger_by_emotion(emotion);
}

// 情绪转换功能：从一个情绪平滑过渡到另一个情绪
esp_err_t ear_transition_emotion(const char* from_emotion, const char* to_emotion, uint32_t transition_time_ms) {
    if (!integration_initialized || from_emotion == nullptr || to_emotion == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Transitioning emotion from %s to %s over %d ms", 
             from_emotion, to_emotion, transition_time_ms);
    
    // 这里可以实现平滑的情绪转换
    // 例如：从happy到sad的过渡，可以先停止happy动作，然后逐渐执行sad动作
    
    // 简单实现：先停止当前动作，然后执行新情绪
    ear_stop_both();
    
    // 等待一小段时间
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 执行新情绪
    return ear_trigger_by_emotion(to_emotion);
}
