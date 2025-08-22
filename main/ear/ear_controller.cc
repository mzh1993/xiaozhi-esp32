#include "ear_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "EAR_CONTROLLER";

EarController::EarController() 
    : sequence_active_(false)
    , sequence_timer_(nullptr)
    , current_step_index_(0)
    , current_loop_count_(0)
    , initialized_(false) {
    
    // 初始化耳朵控制结构
    memset(&left_ear_, 0, sizeof(left_ear_));
    memset(&right_ear_, 0, sizeof(right_ear_));
    
    left_ear_.is_left_ear = true;
    right_ear_.is_left_ear = false;
    
    // 初始化位置状态
    left_ear_position_ = EAR_POSITION_DOWN;
    right_ear_position_ = EAR_POSITION_DOWN;
}

EarController::~EarController() {
    if (initialized_) {
        // 停止所有耳朵动作
        StopBoth();
        
        // 停止序列
        if (sequence_active_) {
            sequence_active_ = false;
            if (sequence_timer_) {
                xTimerStop(sequence_timer_, 0);
            }
        }
        
        // 删除定时器
        if (sequence_timer_) {
            xTimerDelete(sequence_timer_, portMAX_DELAY);
            sequence_timer_ = nullptr;
        }
        
        initialized_ = false;
    }
}

void EarController::SequenceTimerCallback(TimerHandle_t timer) {
    if (!sequence_active_ || current_sequence_.empty()) {
        return;
    }
    
    // 执行当前步骤
    ear_sequence_step_t step = current_sequence_[current_step_index_];
    
    // 执行组合动作
    ear_combo_param_t combo = {step.combo_action, step.duration_ms};
    MoveBoth(combo);
    
    // 移动到下一步
    current_step_index_++;
    
    // 检查序列是否完成
    if (current_step_index_ >= current_sequence_.size()) {
        current_step_index_ = 0;
        current_loop_count_++;
        
        // 检查循环是否完成
        if (!current_loop_count_ || current_loop_count_ >= 1) {
            sequence_active_ = false;
            StopBoth();
            ESP_LOGI(TAG, "Sequence completed");
        }
    }
}

// 默认实现 - 子类可以重写
esp_err_t EarController::StopEar(bool left_ear) {
    ear_action_param_t action = {EAR_ACTION_STOP, 0};
    return MoveEar(left_ear, action);
}

esp_err_t EarController::StopBoth() {
    StopEar(true);
    StopEar(false);
    return ESP_OK;
}

// 默认实现 - 子类可以重写
esp_err_t EarController::SetEarPosition(bool left_ear, ear_position_t position) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "SetEarPosition called for %s ear, position: %d", 
             left_ear ? "left" : "right", position);
    return ESP_OK;
}

ear_position_t EarController::GetEarPosition(bool left_ear) {
    return left_ear ? left_ear_position_ : right_ear_position_;
}

esp_err_t EarController::ResetToDefault() {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "ResetToDefault called");
    return ESP_OK;
}

esp_err_t EarController::PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "PlaySequence called with %d steps, loop: %s", 
             count, loop ? "true" : "false");
    return ESP_OK;
}

esp_err_t EarController::StopSequence() {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "StopSequence called");
    return ESP_OK;
}

esp_err_t EarController::SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "SetEmotion called for emotion: %s, %d steps", 
             emotion ? emotion : "null", count);
    return ESP_OK;
}

esp_err_t EarController::TriggerEmotion(const char* emotion) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "TriggerEmotion called with emotion: %s", emotion ? emotion : "null");
    return ESP_OK;
}

esp_err_t EarController::StopEmotion() {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "StopEmotion called");
    return ESP_OK;
}

ear_action_t EarController::GetCurrentAction(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_action;
}

bool EarController::IsMoving(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->is_active;
}

bool EarController::IsSequenceActive() {
    return sequence_active_;
}
