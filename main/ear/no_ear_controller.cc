#include "no_ear_controller.h"
#include "esp_log.h"

static const char *TAG = "NO_EAR_CONTROLLER";

NoEarController::NoEarController() {
    ESP_LOGI(TAG, "NoEarController created - this is a dummy implementation");
}

NoEarController::~NoEarController() {
    ESP_LOGI(TAG, "NoEarController destroyed");
}

esp_err_t NoEarController::Initialize() {
    ESP_LOGI(TAG, "NoEarController::Initialize called");
    LogOperation("Initialize");
    initialized_ = true;
    ESP_LOGI(TAG, "NoEarController initialized successfully");
    return ESP_OK;
}

esp_err_t NoEarController::Deinitialize() {
    LogOperation("Deinitialize");
    initialized_ = false;
    return ESP_OK;
}

void NoEarController::SetGpioLevels(bool left_ear, ear_direction_t direction) {
    LogOperation("SetGpioLevels", 
                (std::string(left_ear ? "Left" : "Right") + " ear, direction " + 
                 std::to_string(direction)).c_str());
}

esp_err_t NoEarController::SetDirection(bool left_ear, ear_direction_t direction) {
    LogOperation("SetDirection", 
                (std::string(left_ear ? "Left" : "Right") + " ear, direction " + 
                 std::to_string(direction)).c_str());
    return ESP_OK;
}

esp_err_t NoEarController::SetSpeed(bool left_ear, ear_speed_t speed) {
    LogOperation("SetSpeed", left_ear ? "left" : "right");
    return ESP_OK;
}

esp_err_t NoEarController::Stop(bool left_ear) {
    LogOperation("Stop", left_ear ? "left" : "right");
    return ESP_OK;
}

esp_err_t NoEarController::StopBoth() {
    LogOperation("StopBoth");
    return ESP_OK;
}

esp_err_t NoEarController::MoveTimed(bool left_ear, ear_direction_t direction, 
                                    ear_speed_t speed, uint32_t duration_ms) {
    LogOperation("MoveTimed", left_ear ? "left" : "right");
    return ESP_OK;
}

esp_err_t NoEarController::MoveBothTimed(ear_direction_t direction, 
                                        ear_speed_t speed, uint32_t duration_ms) {
    LogOperation("MoveBothTimed");
    return ESP_OK;
}

esp_err_t NoEarController::PlayScenario(ear_scenario_t scenario) {
    LogOperation("PlayScenario", 
                ("scenario " + std::to_string(scenario)).c_str());
    return ESP_OK;
}

esp_err_t NoEarController::PlayScenarioAsync(ear_scenario_t scenario) {
    LogOperation("PlayScenarioAsync");
    return ESP_OK;
}

esp_err_t NoEarController::StopScenario() {
    LogOperation("StopScenario");
    return ESP_OK;
}

esp_err_t NoEarController::PeekabooMode(uint32_t duration_ms) {
    LogOperation("PeekabooMode", 
                ("duration " + std::to_string(duration_ms) + "ms").c_str());
    return ESP_OK;
}

esp_err_t NoEarController::InsectBiteMode(bool left_ear, uint32_t duration_ms) {
    LogOperation("InsectBiteMode", 
                (std::string(left_ear ? "Left" : "Right") + " ear, duration " + 
                 std::to_string(duration_ms) + "ms").c_str());
    return ESP_OK;
}

esp_err_t NoEarController::CuriousMode(uint32_t duration_ms) {
    LogOperation("CuriousMode", 
                ("duration " + std::to_string(duration_ms) + "ms").c_str());
    return ESP_OK;
}

esp_err_t NoEarController::SleepyMode() {
    LogOperation("SleepyMode");
    return ESP_OK;
}

esp_err_t NoEarController::ExcitedMode(uint32_t duration_ms) {
    LogOperation("ExcitedMode", 
                ("duration " + std::to_string(duration_ms) + "ms").c_str());
    return ESP_OK;
}

esp_err_t NoEarController::SadMode() {
    LogOperation("SadMode");
    return ESP_OK;
}

esp_err_t NoEarController::AlertMode() {
    LogOperation("AlertMode");
    return ESP_OK;
}

esp_err_t NoEarController::PlayfulMode(uint32_t duration_ms) {
    LogOperation("PlayfulMode", 
                ("duration " + std::to_string(duration_ms) + "ms").c_str());
    return ESP_OK;
}

esp_err_t NoEarController::PlayCustomPattern(ear_movement_step_t *steps, 
                                           uint8_t step_count, bool loop) {
    LogOperation("PlayCustomPattern", 
                ("steps " + std::to_string(step_count) + ", loop " + 
                 std::string(loop ? "true" : "false")).c_str());
    return ESP_OK;
}

esp_err_t NoEarController::SetCustomScenario(ear_scenario_config_t *config) {
    LogOperation("SetCustomScenario");
    return ESP_OK;
}

esp_err_t NoEarController::TriggerByEmotion(const char* emotion) {
    ESP_LOGI(TAG, "NoEarController::TriggerByEmotion called with emotion: %s", emotion ? emotion : "null");
    LogOperation("TriggerByEmotion", emotion);
    return ESP_OK;
}

esp_err_t NoEarController::SetEmotionMapping(const char* emotion, ear_scenario_t scenario,
                                            uint32_t duration_ms) {
    ESP_LOGI(TAG, "NoEarController::SetEmotionMapping: emotion=%s, scenario=%d, duration=%lu ms", 
             emotion ? emotion : "null", scenario, duration_ms);
    LogOperation("SetEmotionMapping", emotion);
    return ESP_OK;
}

esp_err_t NoEarController::GetEmotionMapping(const char* emotion, emotion_ear_mapping_t* mapping) {
    LogOperation("GetEmotionMapping", emotion);
    return ESP_OK;
}

esp_err_t NoEarController::StopEmotionAction() {
    LogOperation("StopEmotionAction");
    return ESP_OK;
}

ear_direction_t NoEarController::GetCurrentDirection(bool left_ear) {
    LogOperation("GetCurrentDirection", left_ear ? "left" : "right");
    return EAR_STOP;
}

ear_speed_t NoEarController::GetCurrentSpeed(bool left_ear) {
    LogOperation("GetCurrentSpeed", left_ear ? "left" : "right");
    return EAR_SPEED_NORMAL;
}

bool NoEarController::IsMoving(bool left_ear) {
    LogOperation("IsMoving", left_ear ? "left" : "right");
    return false;
}

bool NoEarController::IsScenarioActive() {
    LogOperation("IsScenarioActive");
    return false;
}

esp_err_t NoEarController::TriggerByEmotionWithIntensity(const char* emotion, float intensity) {
    LogOperation("TriggerByEmotionWithIntensity", 
                (std::string(emotion) + ", intensity " + std::to_string(intensity)).c_str());
    return ESP_OK;
}

esp_err_t NoEarController::TransitionEmotion(const char* from_emotion, const char* to_emotion, 
                                           uint32_t transition_time_ms) {
    LogOperation("TransitionEmotion", 
                (std::string(from_emotion) + " -> " + std::string(to_emotion) + 
                 ", time " + std::to_string(transition_time_ms) + "ms").c_str());
    return ESP_OK;
}

// 耳朵位置管理接口实现 - 空实现
ear_position_t NoEarController::GetEarPosition(bool left_ear) {
    LogOperation("GetEarPosition", left_ear ? "left" : "right");
    return EAR_POSITION_DOWN; // 默认返回下垂状态
}

esp_err_t NoEarController::SetEarPosition(bool left_ear, ear_position_t position) {
    LogOperation("SetEarPosition", 
                (std::string(left_ear ? "Left" : "Right") + " ear, position " + 
                 std::to_string(position)).c_str());
    return ESP_OK;
}

esp_err_t NoEarController::ResetEarsToDefaultPosition() {
    LogOperation("ResetEarsToDefaultPosition");
    return ESP_OK;
}

esp_err_t NoEarController::EnsureEarsDown() {
    LogOperation("EnsureEarsDown");
    return ESP_OK;
}

void NoEarController::LogOperation(const char* operation, const char* details) {
    if (details) {
        ESP_LOGI(TAG, "[NO-OP] %s: %s", operation, details);
    } else {
        ESP_LOGI(TAG, "[NO-OP] %s", operation);
    }
}
