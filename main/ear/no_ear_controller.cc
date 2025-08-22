#include "no_ear_controller.h"
#include <esp_log.h>
#include <esp_err.h>

static const char *TAG = "NO_EAR_CONTROLLER";

NoEarController::NoEarController() {
    ESP_LOGI(TAG, "NoEarController created - no physical hardware present");
}

NoEarController::~NoEarController() {
    ESP_LOGI(TAG, "NoEarController destroyed");
}

esp_err_t NoEarController::Initialize() {
    ESP_LOGI(TAG, "NoEarController::Initialize called - no hardware to initialize");
    return InitializeBase();
}

esp_err_t NoEarController::Deinitialize() {
    ESP_LOGI(TAG, "NoEarController::Deinitialize called - no hardware to deinitialize");
    return DeinitializeBase();
}

void NoEarController::SetGpioLevels(bool left_ear, ear_action_t action) {
    LogOperation("SetGpioLevels", "No hardware - operation ignored");
}

// ===== 基础控制接口 - 空实现 =====

esp_err_t NoEarController::MoveEar(bool left_ear, ear_action_param_t action) {
    LogOperation("MoveEar", "No hardware - operation ignored");
    return ESP_OK;
}

esp_err_t NoEarController::StopEar(bool left_ear) {
    LogOperation("StopEar", "No hardware - operation ignored");
    return ESP_OK;
}

esp_err_t NoEarController::StopBoth() {
    LogOperation("StopBoth", "No hardware - operation ignored");
    return ESP_OK;
}

// ===== 双耳组合控制接口 - 空实现 =====

esp_err_t NoEarController::MoveBoth(ear_combo_param_t combo) {
    LogOperation("MoveBoth", "No hardware - operation ignored");
    return ESP_OK;
}

// ===== 位置控制接口 - 空实现 =====

esp_err_t NoEarController::SetEarPosition(bool left_ear, ear_position_t position) {
    LogOperation("SetEarPosition", "No hardware - operation ignored");
    
    // 更新虚拟位置状态
    if (left_ear) {
        left_ear_position_ = position;
    } else {
        right_ear_position_ = position;
    }
    
    return ESP_OK;
}

ear_position_t NoEarController::GetEarPosition(bool left_ear) {
    return left_ear ? left_ear_position_ : right_ear_position_;
}

esp_err_t NoEarController::ResetToDefault() {
    LogOperation("ResetToDefault", "No hardware - operation ignored");
    
    // 更新虚拟位置状态
    left_ear_position_ = EAR_POSITION_DOWN;
    right_ear_position_ = EAR_POSITION_DOWN;
    
    return ESP_OK;
}

// ===== 序列控制接口 - 空实现 =====

esp_err_t NoEarController::PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop) {
    LogOperation("PlaySequence", "No hardware - operation ignored");
    return ESP_OK;
}

esp_err_t NoEarController::StopSequence() {
    LogOperation("StopSequence", "No hardware - operation ignored");
    return ESP_OK;
}

// ===== 情绪控制接口 - 空实现 =====

esp_err_t NoEarController::SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) {
    LogOperation("SetEmotion", "No hardware - operation ignored");
    return ESP_OK;
}

esp_err_t NoEarController::TriggerEmotion(const char* emotion) {
    LogOperation("TriggerEmotion", "No hardware - operation ignored");
    return ESP_OK;
}

esp_err_t NoEarController::StopEmotion() {
    LogOperation("StopEmotion", "No hardware - operation ignored");
    return ESP_OK;
}

// ===== 状态查询接口 - 空实现 =====

ear_action_t NoEarController::GetCurrentAction(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_action;
}

bool NoEarController::IsMoving(bool left_ear) {
    return false; // 没有硬件，永远不会移动
}

bool NoEarController::IsSequenceActive() {
    return false; // 没有硬件，永远不会执行序列
}

// ===== 私有方法 =====

void NoEarController::LogOperation(const char* operation, const char* details) {
    if (details) {
        ESP_LOGI(TAG, "%s: %s", operation, details);
    } else {
        ESP_LOGI(TAG, "%s called", operation);
    }
}
