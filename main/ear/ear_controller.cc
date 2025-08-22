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
    
    // 创建序列定时器
    sequence_timer_ = xTimerCreate("ear_sequence_timer", 
                                 pdMS_TO_TICKS(100), 
                                 pdTRUE, 
                                 this, 
                                 StaticSequenceTimerCallback);
    
    if (sequence_timer_ == NULL) {
        ESP_LOGE(TAG, "Failed to create sequence timer");
    }
}

// 静态回调包装函数，用于定时器
void EarController::StaticSequenceTimerCallback(TimerHandle_t timer) {
    // 从定时器获取控制器实例
    EarController* controller = static_cast<EarController*>(pvTimerGetTimerID(timer));
    if (controller) {
        // 调用实例的虚函数
        controller->SequenceTimerCallback(timer);
    }
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
    OnSequenceTimer(timer);
}

void EarController::OnSequenceTimer(TimerHandle_t timer) {
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
        if (current_loop_count_ >= 1) { // 1表示执行一次
            sequence_active_ = false;
            StopBoth();
            ESP_LOGI(TAG, "Sequence completed");
        } else {
            // 循环之间添加停顿
            vTaskDelay(pdMS_TO_TICKS(100)); // 默认100ms间隔
        }
    }
    
    // 设置下一步的定时器
    if (sequence_active_) {
        uint32_t next_delay = step.delay_ms;
        if (next_delay == 0) {
            next_delay = 100; // 默认100ms间隔
        }
        xTimerChangePeriod(sequence_timer_, pdMS_TO_TICKS(next_delay), 0);
    }
}

// 默认实现 - 子类可以重写
esp_err_t EarController::MoveEar(bool left_ear, ear_action_param_t action) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "MoveEar called for %s ear: action=%d, duration=%lu ms", 
             left_ear ? "left" : "right", action.action, action.duration_ms);
    return ESP_OK;
}

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
esp_err_t EarController::MoveBoth(ear_combo_param_t combo) {
    // 基础实现，子类可以重写
    ESP_LOGI(TAG, "MoveBoth called with combo: %d, duration: %lu ms", 
             combo.combo_action, combo.duration_ms);
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
    if (!steps || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 停止当前序列
    StopSequence();
    
    // 设置序列
    current_sequence_.clear();
    for (uint8_t i = 0; i < count; i++) {
        current_sequence_.push_back(steps[i]);
    }
    
    // 开始序列
    current_step_index_ = 0;
    current_loop_count_ = loop ? 0 : 1; // 0表示无限循环，1表示执行一次
    sequence_active_ = true;
    
    // 启动定时器
    if (sequence_timer_) {
        xTimerStart(sequence_timer_, 0);
    }
    
    ESP_LOGI(TAG, "Started sequence with %d steps, loop: %s", count, loop ? "true" : "false");
    return ESP_OK;
}

esp_err_t EarController::StopSequence() {
    if (sequence_active_) {
        sequence_active_ = false;
        if (sequence_timer_) {
            xTimerStop(sequence_timer_, 0);
        }
        StopBoth();
        ESP_LOGI(TAG, "Sequence stopped");
    }
    return ESP_OK;
}

esp_err_t EarController::SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) {
    if (!emotion || !steps || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 创建序列
    std::vector<ear_sequence_step_t> sequence;
    for (uint8_t i = 0; i < count; i++) {
        sequence.push_back(steps[i]);
    }
    
    // 存储到映射表
    std::string emotion_str(emotion);
    emotion_mappings_[emotion_str] = sequence;
    
    ESP_LOGI(TAG, "Custom emotion mapping set: %s -> %d steps", emotion, count);
    return ESP_OK;
}

esp_err_t EarController::TriggerEmotion(const char* emotion) {
    if (!emotion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings_.find(emotion_str);
    
    if (it == emotion_mappings_.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s", emotion);
        return ESP_ERR_NOT_FOUND;
    }
    
    const std::vector<ear_sequence_step_t>& sequence = it->second;
    
    // 播放序列
    if (!sequence.empty()) {
        return PlaySequence(sequence.data(), sequence.size(), false);
    }
    
    return ESP_OK;
}

esp_err_t EarController::StopEmotion() {
    ESP_LOGI(TAG, "Stopping emotion action");
    return StopSequence();
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

// ===== 基类通用初始化方法 =====

esp_err_t EarController::InitializeBase() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // 检查定时器是否创建成功
    if (sequence_timer_ == NULL) {
        ESP_LOGE(TAG, "Sequence timer not created");
        return ESP_ERR_INVALID_STATE;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "Base ear controller initialized");
    return ESP_OK;
}

esp_err_t EarController::DeinitializeBase() {
    if (!initialized_) {
        return ESP_OK;
    }
    
    // 停止所有序列
    if (sequence_active_) {
        StopSequence();
    }
    
    // 停止所有耳朵
    StopBoth();
    
    // 删除定时器
    if (sequence_timer_ != NULL) {
        xTimerDelete(sequence_timer_, portMAX_DELAY);
        sequence_timer_ = nullptr;
    }
    
    initialized_ = false;
    ESP_LOGI(TAG, "Base ear controller deinitialized");
    return ESP_OK;
}
