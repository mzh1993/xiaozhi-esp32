#include "ear_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "EAR_CONTROLLER";

EarController::EarController() 
    : scenario_active_(false)
    , scenario_timer_(nullptr)
    , current_step_index_(0)
    , current_loop_count_(0)
    , initialized_(false) {
    
    // 初始化耳朵控制结构
    memset(&left_ear_, 0, sizeof(left_ear_));
    memset(&right_ear_, 0, sizeof(right_ear_));
    memset(&current_scenario_, 0, sizeof(current_scenario_));
    
    left_ear_.is_left_ear = true;
    right_ear_.is_left_ear = false;
}

EarController::~EarController() {
    if (initialized_) {
        // 在析构函数中，我们只能调用非虚函数或已经实现的虚函数
        // 停止所有耳朵动作
        StopBoth();
        
        // 停止场景
        if (scenario_active_) {
            scenario_active_ = false;
            if (scenario_timer_) {
                xTimerStop(scenario_timer_, 0);
            }
        }
        
        // 删除定时器
        if (scenario_timer_) {
            xTimerDelete(scenario_timer_, portMAX_DELAY);
            scenario_timer_ = nullptr;
        }
        
        initialized_ = false;
    }
}

uint32_t EarController::SpeedToDelay(ear_speed_t speed) {
    switch (speed) {
        case EAR_SPEED_SLOW: return 50;      // 50ms delay
        case EAR_SPEED_NORMAL: return 20;    // 20ms delay
        case EAR_SPEED_FAST: return 10;      // 10ms delay
        case EAR_SPEED_VERY_FAST: return 5;  // 5ms delay
        default: return 20;
    }
}

void EarController::ApplySpeedControl(bool left_ear, ear_speed_t speed) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    ear->current_speed = speed;
    // Speed control is implemented through timing in movement functions
}

void EarController::ScenarioTimerCallback(TimerHandle_t timer) {
    if (!scenario_active_ || current_scenario_.steps == nullptr) {
        return;
    }
    
    // Execute current step
    ear_movement_step_t *step = &current_scenario_.steps[current_step_index_];
    
    // Apply movement to both ears
    MoveTimed(true, step->direction, step->speed, step->duration_ms);
    MoveTimed(false, step->direction, step->speed, step->duration_ms);
    
    // Move to next step
    current_step_index_++;
    
    // Check if scenario is complete
    if (current_step_index_ >= current_scenario_.step_count) {
        current_step_index_ = 0;
        current_loop_count_++;
        
        // Check if loops are complete
        if (!current_scenario_.loop_enabled || 
            current_loop_count_ >= current_scenario_.loop_count) {
            scenario_active_ = false;
            StopBoth();
            ESP_LOGI(TAG, "Scenario completed");
        }
    }
}

// 默认实现 - 子类可以重写
esp_err_t EarController::Stop(bool left_ear) {
    return SetDirection(left_ear, EAR_STOP);
}

esp_err_t EarController::StopBoth() {
    Stop(true);
    Stop(false);
    return ESP_OK;
}

esp_err_t EarController::MoveBothTimed(ear_direction_t direction, 
                                      ear_speed_t speed, uint32_t duration_ms) {
    MoveTimed(true, direction, speed, duration_ms);
    MoveTimed(false, direction, speed, duration_ms);
    return ESP_OK;
}

esp_err_t EarController::PlayScenarioAsync(ear_scenario_t scenario) {
    return PlayScenario(scenario);  // 默认实现，子类可以重写为真正的异步
}

esp_err_t EarController::StopScenario() {
    if (scenario_active_) {
        scenario_active_ = false;
        if (scenario_timer_) {
            xTimerStop(scenario_timer_, 0);
        }
        StopBoth();
        ESP_LOGI(TAG, "Scenario stopped");
    }
    return ESP_OK;
}

esp_err_t EarController::PeekabooMode(uint32_t duration_ms) {
    StopScenario();
    return MoveBothTimed(EAR_FORWARD, EAR_SPEED_NORMAL, duration_ms);
}

esp_err_t EarController::InsectBiteMode(bool left_ear, uint32_t duration_ms) {
    StopScenario();
    
    // Create rapid back-and-forth movement
    for (int i = 0; i < 10; i++) {
        MoveTimed(left_ear, EAR_BACKWARD, EAR_SPEED_VERY_FAST, 150);
        vTaskDelay(pdMS_TO_TICKS(100));
        MoveTimed(left_ear, EAR_FORWARD, EAR_SPEED_VERY_FAST, 150);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ESP_OK;
}

esp_err_t EarController::CuriousMode(uint32_t duration_ms) {
    StopScenario();
    
    // Alternate ear movement
    for (int i = 0; i < 3; i++) {
        MoveTimed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 1000);
        MoveTimed(false, EAR_BACKWARD, EAR_SPEED_NORMAL, 1000);
        vTaskDelay(pdMS_TO_TICKS(500));
        MoveTimed(true, EAR_BACKWARD, EAR_SPEED_NORMAL, 1000);
        MoveTimed(false, EAR_FORWARD, EAR_SPEED_NORMAL, 1000);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    return ESP_OK;
}

esp_err_t EarController::SleepyMode() {
    StopScenario();
    
    // Slow downward movement
    return MoveBothTimed(EAR_BACKWARD, EAR_SPEED_SLOW, 3000);
}

esp_err_t EarController::ExcitedMode(uint32_t duration_ms) {
    StopScenario();
    
    // Fast alternating movement
    for (int i = 0; i < 10; i++) {
        MoveBothTimed(EAR_FORWARD, EAR_SPEED_FAST, 200);
        vTaskDelay(pdMS_TO_TICKS(100));
        MoveBothTimed(EAR_BACKWARD, EAR_SPEED_FAST, 200);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ESP_OK;
}

esp_err_t EarController::SadMode() {
    StopScenario();
    
    // Droop ears slowly
    return MoveBothTimed(EAR_BACKWARD, EAR_SPEED_SLOW, 2000);
}

esp_err_t EarController::AlertMode() {
    StopScenario();
    
    // Perk up ears quickly
    return MoveBothTimed(EAR_FORWARD, EAR_SPEED_FAST, 500);
}

esp_err_t EarController::PlayfulMode(uint32_t duration_ms) {
    StopScenario();
    
    // Random-like playful movement
    for (int i = 0; i < 8; i++) {
        MoveTimed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 400);
        MoveTimed(false, EAR_BACKWARD, EAR_SPEED_FAST, 300);
        vTaskDelay(pdMS_TO_TICKS(200));
        MoveTimed(true, EAR_BACKWARD, EAR_SPEED_FAST, 200);
        MoveTimed(false, EAR_FORWARD, EAR_SPEED_NORMAL, 500);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    return ESP_OK;
}

esp_err_t EarController::PlayCustomPattern(ear_movement_step_t *steps, 
                                          uint8_t step_count, bool loop) {
    if (!steps || step_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    StopScenario();
    
    // 这里可以实现自定义模式的播放逻辑
    // 子类可以重写此方法提供更复杂的实现
    
    return ESP_OK;
}

esp_err_t EarController::SetCustomScenario(ear_scenario_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    current_scenario_ = *config;
    return ESP_OK;
}

esp_err_t EarController::TriggerByEmotion(const char* emotion) {
    ESP_LOGI(TAG, "TriggerByEmotion called with emotion: %s", emotion ? emotion : "null");
    
    if (!emotion) {
        ESP_LOGE(TAG, "TriggerByEmotion: emotion parameter is null");
        return ESP_ERR_INVALID_ARG;
    }
    
    std::string emotion_str(emotion);
    ESP_LOGI(TAG, "Looking up emotion mapping for: %s", emotion_str.c_str());
    
    auto it = emotion_mappings_.find(emotion_str);
    
    if (it == emotion_mappings_.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s, using neutral", emotion);
        // 未知情绪使用neutral
        it = emotion_mappings_.find("neutral");
        if (it == emotion_mappings_.end()) {
            ESP_LOGE(TAG, "No neutral emotion mapping found, cannot fallback");
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    const emotion_ear_mapping_t& mapping = it->second;
    ESP_LOGI(TAG, "Found emotion mapping: scenario=%d, duration=%lu ms, auto_stop=%s", 
             mapping.ear_scenario, mapping.duration_ms, mapping.auto_stop ? "true" : "false");
    
    ESP_LOGI(TAG, "Triggering ear action for emotion: %s, scenario: %d, duration: %lu ms", 
             emotion, mapping.ear_scenario, mapping.duration_ms);
    
    esp_err_t ret = PlayScenario(mapping.ear_scenario);
    ESP_LOGI(TAG, "PlayScenario result: %s", (ret == ESP_OK) ? "success" : "failed");
    
    return ret;
}

esp_err_t EarController::SetEmotionMapping(const char* emotion, ear_scenario_t scenario, 
                                          uint32_t duration_ms) {
    ESP_LOGI(TAG, "SetEmotionMapping called: emotion=%s, scenario=%d, duration=%lu ms", 
             emotion ? emotion : "null", scenario, duration_ms);
    
    if (!emotion) {
        ESP_LOGE(TAG, "SetEmotionMapping: emotion parameter is null");
        return ESP_ERR_INVALID_ARG;
    }
    
    std::string emotion_str(emotion);
    emotion_ear_mapping_t mapping = {
        .ear_scenario = scenario,
        .duration_ms = duration_ms,
        .auto_stop = true
    };
    
    ESP_LOGI(TAG, "Setting emotion mapping: %s -> scenario %d, duration %lu ms", 
             emotion, scenario, duration_ms);
    
    emotion_mappings_[emotion_str] = mapping;
    ESP_LOGI(TAG, "Emotion mapping set successfully, total mappings: %zu", emotion_mappings_.size());
    
    return ESP_OK;
}

esp_err_t EarController::GetEmotionMapping(const char* emotion, emotion_ear_mapping_t* mapping) {
    if (!emotion || !mapping) {
        return ESP_ERR_INVALID_ARG;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings_.find(emotion_str);
    
    if (it != emotion_mappings_.end()) {
        *mapping = it->second;
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t EarController::StopEmotionAction() {
    ESP_LOGI(TAG, "Stopping current emotion-related ear action");
    return StopBoth();
}

esp_err_t EarController::TriggerByEmotionWithIntensity(const char* emotion, float intensity) {
    if (!emotion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 根据强度调整持续时间
    uint32_t base_duration = 2000;  // 基础持续时间2秒
    uint32_t adjusted_duration = (uint32_t)(base_duration * intensity);
    
    ESP_LOGI(TAG, "Triggering ear action with intensity: %s, intensity: %.2f, duration: %lu ms", 
             emotion, intensity, adjusted_duration);
    
    // 这里可以根据情绪和强度组合实现更复杂的动作
    return TriggerByEmotion(emotion);
}

esp_err_t EarController::TransitionEmotion(const char* from_emotion, const char* to_emotion, 
                                          uint32_t transition_time_ms) {
    if (!from_emotion || !to_emotion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Transitioning emotion from %s to %s over %lu ms", 
             from_emotion, to_emotion, transition_time_ms);
    
    // 简单实现：先停止当前动作，然后执行新情绪
    StopBoth();
    
    // 等待一小段时间
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 执行新情绪
    return TriggerByEmotion(to_emotion);
}

ear_direction_t EarController::GetCurrentDirection(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_direction;
}

ear_speed_t EarController::GetCurrentSpeed(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_speed;
}

bool EarController::IsMoving(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->is_active;
}

bool EarController::IsScenarioActive() {
    return scenario_active_;
}
