#include "tc118s_ear_controller.h"
#include <esp_log.h>
#include <esp_err.h>
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "TC118S_EAR_CONTROLLER";

// 默认情绪序列定义 - 使用延时系数自动计算
const ear_sequence_step_t Tc118sEarController::happy_sequence_[] = {
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_NORMAL_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_NORMAL_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_NORMAL_RATIO), EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_QUICK_RATIO)},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),    0}
};

const ear_sequence_step_t Tc118sEarController::curious_sequence_[] = {
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_FULL_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_FULL_RATIO)},
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_FULL_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_FULL_RATIO)}
};

const ear_sequence_step_t Tc118sEarController::excited_sequence_[] = {
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_NORMAL_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_QUICK_RATIO), EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_NORMAL_RATIO)},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_QUICK_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_QUICK_RATIO), 0}
};

const ear_sequence_step_t Tc118sEarController::playful_sequence_[] = {
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_SLOW_RATIO),    EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_SLOW_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_NORMAL_RATIO), EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_NORMAL_RATIO)},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_QUICK_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_SLOW_RATIO),  EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_SLOW_RATIO)},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_NORMAL_RATIO),  EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_NORMAL_RATIO)},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_NORMAL_RATIO), 0}
};

const ear_sequence_step_t Tc118sEarController::sad_sequence_[] = {
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_SLOW_RATIO),    0},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),    EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_SLOW_RATIO)}
};

const ear_sequence_step_t Tc118sEarController::surprised_sequence_[] = {
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),    0},
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_SLOW_RATIO),   EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_SLOW_RATIO)}
};

const ear_sequence_step_t Tc118sEarController::sleepy_sequence_[] = {
    {EAR_COMBO_BOTH_BACKWARD, EMOTION_TIME(EAR_POSITION_DOWN_TIME_MS, EMOTION_FULL_RATIO),   0},
    {EAR_COMBO_BOTH_FORWARD,  EMOTION_TIME(EAR_POSITION_UP_TIME_MS, EMOTION_QUICK_RATIO),    EMOTION_GAP(EAR_POSITION_MIDDLE_TIME_MS, EMOTION_GAP_SLOW_RATIO)}
};

// 默认情绪映射 - 按类别分组，便于统一调整
const std::map<std::string, std::vector<ear_sequence_step_t>> Tc118sEarController::default_emotion_mappings_ = {
    // ===== 中性/无动作情绪 =====
    {"neutral", {}},      // 中性：无动作
    {"relaxed", {}},      // 放松：无动作
    
    // ===== 开心类情绪 - 使用 happy_sequence_ =====
    {"happy", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}},
    
    // ===== 兴奋类情绪 - 使用 excited_sequence_ =====
    {"excited", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},
    {"laughing", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},  // 大笑：用兴奋序列
    {"delicious", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}}, // 美味：用兴奋序列
    
    // ===== 顽皮类情绪 - 使用 playful_sequence_ =====
    {"playful", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},
    {"funny", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},     // 有趣：用顽皮序列
    {"silly", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},    // 傻傻的：用顽皮序列
    {"winking", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + 2)}},  // 眨眼：用顽皮序列前2步（固定长度）
    
    // ===== 悲伤类情绪 - 使用 sad_sequence_ =====
    {"sad", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},
    {"crying", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},           // 哭泣：用悲伤序列
    {"embarrassed", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},     // 尴尬：用悲伤序列
    
    // ===== 惊讶类情绪 - 使用 surprised_sequence_ =====
    {"surprised", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}},
    {"shocked", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}}, // 震惊：用惊讶序列
    {"angry", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}},  // 愤怒：用惊讶序列
    {"cool", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}},   // 酷：用惊讶序列
    {"confident", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}}, // 自信：用惊讶序列
    
    // ===== 好奇类情绪 - 使用 curious_sequence_ =====
    {"curious", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},
    {"loving", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},    // 爱意：用好奇序列
    {"thinking", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},  // 思考：用好奇序列
    {"kissy", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},     // 亲吻：用好奇序列
    {"confused", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},  // 困惑：用好奇序列
    
    // ===== 特殊情绪 - 使用独立序列 =====
    {"sleepy", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},     // 困倦：独特的下垂动作
};

Tc118sEarController::Tc118sEarController(gpio_num_t left_ina_pin, gpio_num_t left_inb_pin,
                                       gpio_num_t right_ina_pin, gpio_num_t right_inb_pin)
    : left_ina_pin_(left_ina_pin)
    , left_inb_pin_(left_inb_pin)
    , right_ina_pin_(right_ina_pin)
    , right_inb_pin_(right_inb_pin)
    , current_emotion_("neutral")
    , last_emotion_time_(0)
    , emotion_action_active_(false)
    , left_ear_position_(EAR_POSITION_DOWN)
    , right_ear_position_(EAR_POSITION_DOWN) {
    
    ESP_LOGI(TAG, "TC118S Ear Controller created with pins: L_INA=%d, L_INB=%d, R_INA=%d, R_INB=%d",
             left_ina_pin_, left_inb_pin_, right_ina_pin_, right_inb_pin_);
}

Tc118sEarController::~Tc118sEarController() {
    if (initialized_) {
        Deinitialize();
    }
}

esp_err_t Tc118sEarController::Initialize() {
    ESP_LOGI(TAG, "Initializing TC118S ear controller");
    
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // 初始化左耳
    left_ear_.ina_pin = left_ina_pin_;
    left_ear_.inb_pin = left_inb_pin_;
    left_ear_.is_left_ear = true;
    left_ear_.current_action = EAR_ACTION_STOP;
    left_ear_.is_active = false;
    
    // 初始化右耳
    right_ear_.ina_pin = right_ina_pin_;
    right_ear_.inb_pin = right_inb_pin_;
    right_ear_.is_left_ear = false;
    right_ear_.current_action = EAR_ACTION_STOP;
    right_ear_.is_active = false;
    
    // 配置GPIO引脚
    gpio_reset_pin(left_ina_pin_);
    gpio_reset_pin(left_inb_pin_);
    gpio_reset_pin(right_ina_pin_);
    gpio_reset_pin(right_inb_pin_);
    
    gpio_set_direction(left_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(left_inb_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_inb_pin_, GPIO_MODE_OUTPUT);
    
    // 初始化所有引脚为低电平（停止状态）
    gpio_set_level(left_ina_pin_, 0);
    gpio_set_level(left_inb_pin_, 0);
    gpio_set_level(right_ina_pin_, 0);
    gpio_set_level(right_inb_pin_, 0);
    
    // 创建序列定时器
    sequence_timer_ = xTimerCreate("ear_sequence_timer", 
                                 pdMS_TO_TICKS(100), 
                                 pdTRUE, 
                                 this, 
                                 SequenceTimerCallbackWrapper);
    
    if (sequence_timer_ == NULL) {
        ESP_LOGE(TAG, "Failed to create sequence timer");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化默认情绪映射
    InitializeDefaultEmotionMappings();
    
    // 设置序列模式
    SetupSequencePatterns();
    
    initialized_ = true;
    ESP_LOGI(TAG, "TC118S ear controller initialized successfully");
    return ESP_OK;
}

esp_err_t Tc118sEarController::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing TC118S ear controller");
    
    if (!initialized_) {
        return ESP_OK;
    }
    
    // 停止所有序列
    if (sequence_active_) {
        StopSequence();
    }
    
    // 确保耳朵回到下垂状态
    ResetToDefault();
    
    // 等待耳朵动作完成
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 停止所有耳朵
    StopBoth();
    
    // 删除定时器
    if (sequence_timer_ != NULL) {
        xTimerDelete(sequence_timer_, portMAX_DELAY);
        sequence_timer_ = NULL;
    }
    
    initialized_ = false;
    ESP_LOGI(TAG, "TC118S ear controller deinitialized");
    return ESP_OK;
}

void Tc118sEarController::SetGpioLevels(bool left_ear, ear_action_t action) {
    gpio_num_t ina_pin = left_ear ? left_ina_pin_ : right_ina_pin_;
    gpio_num_t inb_pin = left_ear ? left_inb_pin_ : right_inb_pin_;
    
    switch (action) {
        case EAR_ACTION_STOP:
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 0);
            break;
        case EAR_ACTION_FORWARD:
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 0);
            break;
        case EAR_ACTION_BACKWARD:
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 1);
            break;
        case EAR_ACTION_BRAKE:
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 1);
            break;
    }
    
    // 更新状态
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    ear->current_action = action;
    ear->is_active = (action != EAR_ACTION_STOP);
}

// ===== 基础控制接口实现 =====

esp_err_t Tc118sEarController::MoveEar(bool left_ear, ear_action_param_t action) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Moving %s ear: action=%d, duration=%lu ms", 
             left_ear ? "left" : "right", action.action, action.duration_ms);
    
    // 设置GPIO状态
    SetGpioLevels(left_ear, action.action);
    
    // 运行指定时间
    if (action.duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(action.duration_ms));
        // 停止动作
        StopEar(left_ear);
    }
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopEar(bool left_ear) {
    ear_action_param_t action = {EAR_ACTION_STOP, 0};
    return MoveEar(left_ear, action);
}

esp_err_t Tc118sEarController::StopBoth() {
    StopEar(true);
    StopEar(false);
    return ESP_OK;
}

// ===== 双耳组合控制接口实现 =====

esp_err_t Tc118sEarController::MoveBoth(ear_combo_param_t combo) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Moving both ears: combo=%d, duration=%lu ms", 
             combo.combo_action, combo.duration_ms);
    
    // 根据组合动作类型设置双耳GPIO状态
    switch (combo.combo_action) {
        case EAR_COMBO_BOTH_FORWARD:
            SetGpioLevels(true, EAR_ACTION_FORWARD);
            SetGpioLevels(false, EAR_ACTION_FORWARD);
            break;
            
        case EAR_COMBO_BOTH_BACKWARD:
            SetGpioLevels(true, EAR_ACTION_BACKWARD);
            SetGpioLevels(false, EAR_ACTION_BACKWARD);
            break;
            
        case EAR_COMBO_BOTH_STOP:
            SetGpioLevels(true, EAR_ACTION_STOP);
            SetGpioLevels(false, EAR_ACTION_STOP);
            break;
            
        case EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD:
            SetGpioLevels(true, EAR_ACTION_FORWARD);
            // 右耳保持当前状态，不改变
            break;
            
        case EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD:
            // 左耳保持当前状态，不改变
            SetGpioLevels(false, EAR_ACTION_FORWARD);
            break;
            
        case EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD:
            SetGpioLevels(true, EAR_ACTION_FORWARD);
            SetGpioLevels(false, EAR_ACTION_BACKWARD);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown combo action: %d", combo.combo_action);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 运行指定时间
    if (combo.duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(combo.duration_ms));
        // 停止所有耳朵
        StopBoth();
    }
    
    return ESP_OK;
}

// ===== 位置控制接口实现 =====

esp_err_t Tc118sEarController::SetEarPosition(bool left_ear, ear_position_t position) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ear_action_param_t action;
    
    switch (position) {
        case EAR_POSITION_DOWN:
            action = {EAR_ACTION_BACKWARD, EAR_POSITION_DOWN_TIME_MS};
            break;
        case EAR_POSITION_UP:
            action = {EAR_ACTION_FORWARD, EAR_POSITION_UP_TIME_MS};
            break;
        case EAR_POSITION_MIDDLE: {
            // 根据当前位置计算需要的动作
            ear_position_t current = GetEarPosition(left_ear);
            if (current == EAR_POSITION_UP) {
                action = {EAR_ACTION_BACKWARD, EAR_POSITION_MIDDLE_TIME_MS};
            } else {
                action = {EAR_ACTION_FORWARD, EAR_POSITION_MIDDLE_TIME_MS};
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown ear position: %d", position);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 执行动作
    esp_err_t ret = MoveEar(left_ear, action);
    
    // 更新位置状态
    if (ret == ESP_OK) {
        if (left_ear) {
            left_ear_position_ = position;
        } else {
            right_ear_position_ = position;
        }
    }
    
    return ret;
}

ear_position_t Tc118sEarController::GetEarPosition(bool left_ear) {
    return left_ear ? left_ear_position_ : right_ear_position_;
}

esp_err_t Tc118sEarController::ResetToDefault() {
    ESP_LOGI(TAG, "Resetting ears to default position (DOWN)");
    
    esp_err_t ret1 = SetEarPosition(true, EAR_POSITION_DOWN);
    esp_err_t ret2 = SetEarPosition(false, EAR_POSITION_DOWN);
    
    if (ret1 == ESP_OK && ret2 == ESP_OK) {
        ESP_LOGI(TAG, "Ears reset to default position successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to reset ears to default position");
        return ESP_FAIL;
    }
}

// ===== 序列控制接口实现 =====

esp_err_t Tc118sEarController::PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop) {
    if (!steps || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    StopSequence();
    
    // 设置序列
    current_sequence_.clear();
    for (uint8_t i = 0; i < count; i++) {
        current_sequence_.push_back(steps[i]);
    }
    
    // 开始序列
    current_step_index_ = 0;
    current_loop_count_ = 0;
    sequence_active_ = true;
    
    // 启动定时器
    xTimerStart(sequence_timer_, 0);
    
    ESP_LOGI(TAG, "Started sequence with %d steps, loop: %s", count, loop ? "true" : "false");
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopSequence() {
    if (sequence_active_) {
        sequence_active_ = false;
        emotion_action_active_ = false;
        if (sequence_timer_) {
            xTimerStop(sequence_timer_, 0);
        }
        StopBoth();
        ESP_LOGI(TAG, "Sequence stopped");
    }
    return ESP_OK;
}

// ===== 情绪控制接口实现 =====

esp_err_t Tc118sEarController::SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) {
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

esp_err_t Tc118sEarController::TriggerEmotion(const char* emotion) {
    if (!emotion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否应该触发情绪
    if (!ShouldTriggerEmotion(emotion)) {
        ESP_LOGI(TAG, "Emotion trigger skipped for: %s", emotion);
        return ESP_OK;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings_.find(emotion_str);
    
    if (it == emotion_mappings_.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s", emotion);
        return ESP_ERR_NOT_FOUND;
    }
    
    const std::vector<ear_sequence_step_t>& sequence = it->second;
    
    // 更新情绪状态
    UpdateEmotionState(emotion);
    
    // 播放序列
    if (!sequence.empty()) {
        return PlaySequence(sequence.data(), sequence.size(), false);
    }
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopEmotion() {
    ESP_LOGI(TAG, "Stopping emotion action");
    return StopSequence();
}

// ===== 状态查询接口实现 =====

ear_action_t Tc118sEarController::GetCurrentAction(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_action;
}

bool Tc118sEarController::IsMoving(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->is_active;
}

bool Tc118sEarController::IsSequenceActive() {
    return sequence_active_;
}

// ===== 私有方法实现 =====

void Tc118sEarController::InitializeDefaultEmotionMappings() {
    // 复制默认映射到实例映射表
    for (const auto& pair : default_emotion_mappings_) {
        emotion_mappings_[pair.first] = pair.second;
    }
    ESP_LOGI(TAG, "Default emotion mappings initialized");
}

void Tc118sEarController::SetupSequencePatterns() {
    ESP_LOGI(TAG, "Sequence patterns setup completed");
}

void Tc118sEarController::SequenceTimerCallbackWrapper(TimerHandle_t timer) {
    Tc118sEarController* controller = static_cast<Tc118sEarController*>(pvTimerGetTimerID(timer));
    if (controller) {
        controller->InternalSequenceTimerCallback(timer);
    }
}

void Tc118sEarController::InternalSequenceTimerCallback(TimerHandle_t timer) {
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
            emotion_action_active_ = false;
            
            // 设置耳朵的最终位置
            SetEarFinalPosition();
            
            StopBoth();
            ESP_LOGI(TAG, "Sequence completed");
        } else {
            // 循环之间添加停顿
            vTaskDelay(pdMS_TO_TICKS(SCENARIO_LOOP_DELAY_MS));
        }
    }
    
    // 设置下一步的定时器
    if (sequence_active_) {
        uint32_t next_delay = step.delay_ms;
        if (next_delay == 0) {
            next_delay = SCENARIO_DEFAULT_DELAY_MS;
        }
        xTimerChangePeriod(sequence_timer_, pdMS_TO_TICKS(next_delay), 0);
    }
}

bool Tc118sEarController::ShouldTriggerEmotion(const char* emotion) {
    if (!emotion) {
        return false;
    }
    
    // 获取当前时间
    uint64_t current_time = esp_timer_get_time() / 1000;
    
    // 如果情绪相同且还在冷却期内，不触发
    if (current_emotion_ == emotion && 
        (current_time - last_emotion_time_) < EMOTION_COOLDOWN_MS) {
        ESP_LOGI(TAG, "Emotion %s still in cooldown, skipping trigger", emotion);
        return false;
    }
    
    // 如果当前有情绪动作正在进行，不触发新的情绪
    if (emotion_action_active_) {
        ESP_LOGI(TAG, "Emotion action already active, skipping trigger for %s", emotion);
        return false;
    }
    
    return true;
}

void Tc118sEarController::UpdateEmotionState(const char* emotion) {
    if (!emotion) {
        return;
    }
    
    current_emotion_ = emotion;
    last_emotion_time_ = esp_timer_get_time() / 1000;
    emotion_action_active_ = true;
    
    ESP_LOGI(TAG, "Updated emotion state: %s, time: %llu", emotion, last_emotion_time_);
}

void Tc118sEarController::SetEarFinalPosition() {
    // 设置耳朵到默认下垂位置
    ESP_LOGI(TAG, "Setting ears to default DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
}


