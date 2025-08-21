#include "tc118s_ear_controller.h"
#include <esp_log.h>
#include <esp_err.h>
#include "esp_timer.h"
#include <string.h>
#include <random>

static const char *TAG = "TC118S_EAR_CONTROLLER";

// 场景模式定义
ear_movement_step_t Tc118sEarController::peekaboo_steps_[] = {
    {EAR_FORWARD, EAR_SPEED_NORMAL, 5000, 0}  // 5秒向前
};

ear_movement_step_t Tc118sEarController::insect_bite_steps_[] = {
    {EAR_BACKWARD, EAR_SPEED_VERY_FAST, 200, 100},
    {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100},
    {EAR_BACKWARD, EAR_SPEED_VERY_FAST, 200, 100},
    {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100}
};

ear_movement_step_t Tc118sEarController::curious_steps_[] = {
    {EAR_FORWARD, EAR_SPEED_NORMAL, 1000, 500},
    {EAR_BACKWARD, EAR_SPEED_NORMAL, 1000, 500}
};

ear_movement_step_t Tc118sEarController::excited_steps_[] = {
    {EAR_FORWARD, EAR_SPEED_FAST, 300, 200},
    {EAR_BACKWARD, EAR_SPEED_FAST, 300, 200}
};

ear_movement_step_t Tc118sEarController::playful_steps_[] = {
    {EAR_FORWARD, EAR_SPEED_NORMAL, 800, 300},
    {EAR_BACKWARD, EAR_SPEED_FAST, 400, 200},
    {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100},
    {EAR_BACKWARD, EAR_SPEED_NORMAL, 600, 400}
};

// 默认情绪映射
const std::map<std::string, emotion_ear_mapping_t> Tc118sEarController::default_emotion_mappings_ = {
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

Tc118sEarController::Tc118sEarController(gpio_num_t left_ina_pin, gpio_num_t left_inb_pin,
                                       gpio_num_t right_ina_pin, gpio_num_t right_inb_pin)
    : left_ina_pin_(left_ina_pin)
    , left_inb_pin_(left_inb_pin)
    , right_ina_pin_(right_ina_pin)
    , right_inb_pin_(right_inb_pin) {
    
    ESP_LOGI(TAG, "TC118S Ear Controller created with pins: L_INA=%d, L_INB=%d, R_INA=%d, R_INB=%d",
             left_ina_pin_, left_inb_pin_, right_ina_pin_, right_inb_pin_);
}

Tc118sEarController::~Tc118sEarController() {
    if (initialized_) {
        Deinitialize();
    }
}

esp_err_t Tc118sEarController::Initialize() {
    ESP_LOGI(TAG, "Tc118sEarController::Initialize called");
    
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing TC118S ear controller with pins: L_INA=%d, L_INB=%d, R_INA=%d, R_INB=%d",
             left_ina_pin_, left_inb_pin_, right_ina_pin_, right_inb_pin_);
    
    // 初始化左耳
    left_ear_.ina_pin = left_ina_pin_;
    left_ear_.inb_pin = left_inb_pin_;
    left_ear_.is_left_ear = true;
    left_ear_.current_direction = EAR_STOP;
    left_ear_.current_speed = EAR_SPEED_NORMAL;
    left_ear_.is_active = false;
    ESP_LOGI(TAG, "Left ear initialized");
    
    // 初始化右耳
    right_ear_.ina_pin = right_ina_pin_;
    right_ear_.inb_pin = right_inb_pin_;
    right_ear_.is_left_ear = false;
    right_ear_.current_direction = EAR_STOP;
    right_ear_.current_speed = EAR_SPEED_NORMAL;
    right_ear_.is_active = false;
    ESP_LOGI(TAG, "Right ear initialized");
    
    // 配置GPIO引脚
    ESP_LOGI(TAG, "Configuring GPIO pins");
    // 逐个配置每个引脚，避免结构体字段顺序问题
    gpio_reset_pin(left_ina_pin_);
    gpio_reset_pin(left_inb_pin_);
    gpio_reset_pin(right_ina_pin_);
    gpio_reset_pin(right_inb_pin_);
    ESP_LOGI(TAG, "GPIO pins reset");
    
    gpio_set_direction(left_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(left_inb_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_inb_pin_, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "GPIO directions set");
    
    // 初始化所有引脚为低电平（停止状态）
    gpio_set_level(left_ina_pin_, 0);
    gpio_set_level(left_inb_pin_, 0);
    gpio_set_level(right_ina_pin_, 0);
    gpio_set_level(right_inb_pin_, 0);
    ESP_LOGI(TAG, "GPIO levels initialized to 0");
    
    // 创建场景定时器
    ESP_LOGI(TAG, "Creating scenario timer");
    scenario_timer_ = xTimerCreate("ear_scenario_timer", 
                                 pdMS_TO_TICKS(100), 
                                 pdTRUE, 
                                 this, 
                                 ScenarioTimerCallbackWrapper);
    
    if (scenario_timer_ == NULL) {
        ESP_LOGE(TAG, "Failed to create scenario timer");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Scenario timer created successfully");
    
    // 初始化默认情绪映射
    ESP_LOGI(TAG, "Initializing default emotion mappings");
    InitializeDefaultEmotionMappings();
    
    // 设置场景模式
    ESP_LOGI(TAG, "Setting up scenario patterns");
    SetupScenarioPatterns();
    
    initialized_ = true;
    ESP_LOGI(TAG, "TC118S ear controller initialized successfully");
    return ESP_OK;
}

esp_err_t Tc118sEarController::Deinitialize() {
    if (!initialized_) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing TC118S ear controller");
    
    // 停止所有耳朵
    StopBoth();
    
    // 删除定时器
    if (scenario_timer_ != NULL) {
        xTimerDelete(scenario_timer_, portMAX_DELAY);
        scenario_timer_ = NULL;
    }
    
    initialized_ = false;
    ESP_LOGI(TAG, "TC118S ear controller deinitialized");
    return ESP_OK;
}

void Tc118sEarController::SetGpioLevels(bool left_ear, ear_direction_t direction) {
    ESP_LOGI(TAG, "SetGpioLevels: %s ear, direction=%d", left_ear ? "Left" : "Right", direction);
    
    gpio_num_t ina_pin = left_ear ? left_ina_pin_ : right_ina_pin_;
    gpio_num_t inb_pin = left_ear ? left_inb_pin_ : right_inb_pin_;
    
    ESP_LOGI(TAG, "Setting GPIO levels: INA=%d, INB=%d", ina_pin, inb_pin);
    
    switch (direction) {
        case EAR_STOP:
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 0);
            ESP_LOGI(TAG, "GPIO levels set: INA=0, INB=0 (STOP)");
            break;
        case EAR_FORWARD:
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 0);
            ESP_LOGI(TAG, "GPIO levels set: INA=1, INB=0 (FORWARD)");
            break;
        case EAR_BACKWARD:
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 1);
            ESP_LOGI(TAG, "GPIO levels set: INA=0, INB=1 (BACKWARD)");
            break;
        case EAR_BRAKE:
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 1);
            ESP_LOGI(TAG, "GPIO levels set: INA=1, INB=1 (BRAKE)");
            break;
    }
    
    // 更新状态
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    ear->current_direction = direction;
    ear->is_active = (direction != EAR_STOP);
    
    ESP_LOGI(TAG, "Ear state updated: direction=%d, active=%s", 
             ear->current_direction, ear->is_active ? "true" : "false");
}

esp_err_t Tc118sEarController::SetDirection(bool left_ear, ear_direction_t direction) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    SetGpioLevels(left_ear, direction);
    
    ESP_LOGI(TAG, "%s ear direction set to %d", 
             left_ear ? "Left" : "Right", direction);
    return ESP_OK;
}

esp_err_t Tc118sEarController::SetSpeed(bool left_ear, ear_speed_t speed) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ApplySpeedControl(left_ear, speed);
    
    ESP_LOGI(TAG, "%s ear speed set to %d", 
             left_ear ? "Left" : "Right", speed);
    return ESP_OK;
}

esp_err_t Tc118sEarController::MoveTimed(bool left_ear, ear_direction_t direction, 
                                        ear_speed_t speed, uint32_t duration_ms) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 设置方向和速度
    SetDirection(left_ear, direction);
    SetSpeed(left_ear, speed);
    
    // 如果有持续时间，创建定时器来停止耳朵
    if (duration_ms > 0) {
        // 使用 FreeRTOS 任务延迟来简化实现
        // 在实际应用中，这应该使用定时器回调
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        Stop(left_ear);
    }
    
    ESP_LOGI(TAG, "%s ear moving %d at speed %d for %lu ms", 
             left_ear ? "Left" : "Right", direction, speed, duration_ms);
    return ESP_OK;
}

esp_err_t Tc118sEarController::PlayScenario(ear_scenario_t scenario) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Playing scenario: %d", scenario);
    
    // 停止当前场景
    StopScenario();
    
    // 根据场景类型配置
    switch (scenario) {
        case EAR_SCENARIO_PEEKABOO:
            current_scenario_.steps = peekaboo_steps_;
            current_scenario_.step_count = 1;
            current_scenario_.loop_enabled = false;
            break;
            
        case EAR_SCENARIO_INSECT_BITE:
            current_scenario_.steps = insect_bite_steps_;
            current_scenario_.step_count = 4;
            current_scenario_.loop_enabled = true;
            current_scenario_.loop_count = 5;  // 重复5次
            break;
            
        case EAR_SCENARIO_CURIOUS:
            current_scenario_.steps = curious_steps_;
            current_scenario_.step_count = 2;
            current_scenario_.loop_enabled = true;
            current_scenario_.loop_count = 3;
            break;
            
        case EAR_SCENARIO_EXCITED:
            current_scenario_.steps = excited_steps_;
            current_scenario_.step_count = 2;
            current_scenario_.loop_enabled = true;
            current_scenario_.loop_count = 8;
            break;
            
        case EAR_SCENARIO_PLAYFUL:
            current_scenario_.steps = playful_steps_;
            current_scenario_.step_count = 4;
            current_scenario_.loop_enabled = true;
            current_scenario_.loop_count = 4;
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown scenario: %d", scenario);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 开始场景
    current_step_index_ = 0;
    current_loop_count_ = 0;
    scenario_active_ = true;
    
    // 启动定时器
    xTimerStart(scenario_timer_, 0);
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::PlayScenarioAsync(ear_scenario_t scenario) {
    // 创建任务来异步执行场景
    return PlayScenario(scenario);  // 简化实现
}

void Tc118sEarController::InitializeDefaultEmotionMappings() {
    // 复制默认映射到实例映射表
    for (const auto& pair : default_emotion_mappings_) {
        emotion_mappings_[pair.first] = pair.second;
    }
    ESP_LOGI(TAG, "Default emotion mappings initialized");
}

void Tc118sEarController::SetupScenarioPatterns() {
    ESP_LOGI(TAG, "Scenario patterns setup completed");
}

void Tc118sEarController::ScenarioTimerCallbackWrapper(TimerHandle_t timer) {
    Tc118sEarController* controller = static_cast<Tc118sEarController*>(pvTimerGetTimerID(timer));
    if (controller) {
        controller->InternalScenarioTimerCallback(timer);
    }
}

void Tc118sEarController::InternalScenarioTimerCallback(TimerHandle_t timer) {
    if (!scenario_active_ || current_scenario_.steps == nullptr) {
        return;
    }
    
    // 执行当前步骤
    ear_movement_step_t *step = &current_scenario_.steps[current_step_index_];
    
    // 应用到双耳
    MoveTimed(true, step->direction, step->speed, step->duration_ms);
    MoveTimed(false, step->direction, step->speed, step->duration_ms);
    
    // 移动到下一步
    current_step_index_++;
    
    // 检查场景是否完成
    if (current_step_index_ >= current_scenario_.step_count) {
        current_step_index_ = 0;
        current_loop_count_++;
        
        // 检查循环是否完成
        if (!current_scenario_.loop_enabled || 
            current_loop_count_ >= current_scenario_.loop_count) {
            scenario_active_ = false;
            StopBoth();
            ESP_LOGI(TAG, "Scenario completed");
        }
    }
}

// 重写特定场景接口
esp_err_t Tc118sEarController::PeekabooMode(uint32_t duration_ms) {
    return EarController::PeekabooMode(duration_ms);
}

esp_err_t Tc118sEarController::InsectBiteMode(bool left_ear, uint32_t duration_ms) {
    return EarController::InsectBiteMode(left_ear, duration_ms);
}

esp_err_t Tc118sEarController::CuriousMode(uint32_t duration_ms) {
    return EarController::CuriousMode(duration_ms);
}

esp_err_t Tc118sEarController::SleepyMode() {
    return EarController::SleepyMode();
}

esp_err_t Tc118sEarController::ExcitedMode(uint32_t duration_ms) {
    return EarController::ExcitedMode(duration_ms);
}

esp_err_t Tc118sEarController::SadMode() {
    return EarController::SadMode();
}

esp_err_t Tc118sEarController::AlertMode() {
    return EarController::AlertMode();
}

esp_err_t Tc118sEarController::PlayfulMode(uint32_t duration_ms) {
    return EarController::PlayfulMode(duration_ms);
}

esp_err_t Tc118sEarController::PlayCustomPattern(ear_movement_step_t *steps, 
                                                uint8_t step_count, bool loop) {
    if (!steps || step_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    StopScenario();
    
    // 设置自定义场景
    current_scenario_.steps = steps;
    current_scenario_.step_count = step_count;
    current_scenario_.loop_enabled = loop;
    current_scenario_.loop_count = loop ? 1 : 0;
    
    // 开始场景
    current_step_index_ = 0;
    current_loop_count_ = 0;
    scenario_active_ = true;
    
    // 启动定时器
    xTimerStart(scenario_timer_, 0);
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::TriggerByEmotion(const char* emotion) {
    return EarController::TriggerByEmotion(emotion);
}

esp_err_t Tc118sEarController::SetEmotionMapping(const char* emotion, ear_scenario_t scenario, 
                                                uint32_t duration_ms) {
    return EarController::SetEmotionMapping(emotion, scenario, duration_ms);
}

esp_err_t Tc118sEarController::TriggerByEmotionWithIntensity(const char* emotion, float intensity) {
    return EarController::TriggerByEmotionWithIntensity(emotion, intensity);
}

esp_err_t Tc118sEarController::TransitionEmotion(const char* from_emotion, const char* to_emotion, 
                                                uint32_t transition_time_ms) {
    return EarController::TransitionEmotion(from_emotion, to_emotion, transition_time_ms);
}

// 添加缺失的纯虚函数实现
esp_err_t Tc118sEarController::Stop(bool left_ear) {
    return EarController::Stop(left_ear);
}

esp_err_t Tc118sEarController::StopBoth() {
    return EarController::StopBoth();
}

esp_err_t Tc118sEarController::MoveBothTimed(ear_direction_t direction, 
                                            ear_speed_t speed, uint32_t duration_ms) {
    return EarController::MoveBothTimed(direction, speed, duration_ms);
}

esp_err_t Tc118sEarController::StopScenario() {
    return EarController::StopScenario();
}

esp_err_t Tc118sEarController::SetCustomScenario(ear_scenario_config_t *config) {
    return EarController::SetCustomScenario(config);
}

esp_err_t Tc118sEarController::GetEmotionMapping(const char* emotion, emotion_ear_mapping_t* mapping) {
    return EarController::GetEmotionMapping(emotion, mapping);
}

esp_err_t Tc118sEarController::StopEmotionAction() {
    return EarController::StopEmotionAction();
}

ear_direction_t Tc118sEarController::GetCurrentDirection(bool left_ear) {
    return EarController::GetCurrentDirection(left_ear);
}

ear_speed_t Tc118sEarController::GetCurrentSpeed(bool left_ear) {
    return EarController::GetCurrentSpeed(left_ear);
}

bool Tc118sEarController::IsMoving(bool left_ear) {
    return EarController::IsMoving(left_ear);
}

bool Tc118sEarController::IsScenarioActive() {
    return EarController::IsScenarioActive();
}
