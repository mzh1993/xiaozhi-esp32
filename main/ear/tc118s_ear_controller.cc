#include "tc118s_ear_controller.h"
#include <esp_log.h>
#include <esp_err.h>
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "TC118S_EAR_CONTROLLER";

// 重新设计的情绪序列 - 基于时间控制，创造不同的运动节奏和表达效果
const ear_sequence_step_t Tc118sEarController::happy_sequence_[] = {
    // 开心：快速节奏，短时间动作，表达活泼
    {EAR_COMBO_BOTH_FORWARD,  80,  150},  // 快速竖起80ms，停顿150ms
    {EAR_COMBO_BOTH_BACKWARD, 80,  150},  // 快速下垂80ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD,  80,  150},  // 再次快速竖起
    {EAR_COMBO_BOTH_FORWARD,  40,  0}     // 最后调整到居中状态（竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::curious_sequence_[] = {
    // 好奇：左右耳交替，中等节奏，模拟"倾听"动作
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD,  100, 200},  // 左耳竖起100ms，停顿200ms
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD,  100, 200},  // 右耳竖起100ms，停顿200ms
    {EAR_COMBO_BOTH_FORWARD, 80,  150}, // 双耳同时竖起80ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD, 40,  0}     // 最后调整到居中状态（竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::excited_sequence_[] = {
    // 兴奋：超快速节奏，短时间动作，表达激动
    {EAR_COMBO_BOTH_FORWARD,  60,  100},  // 超快速竖起60ms，短停顿100ms
    {EAR_COMBO_BOTH_BACKWARD, 60,  100},  // 超快速下垂60ms，短停顿100ms
    {EAR_COMBO_BOTH_FORWARD,  60,  100},  // 再次超快速竖起
    {EAR_COMBO_BOTH_BACKWARD, 60,  100},  // 再次超快速下垂
    {EAR_COMBO_BOTH_FORWARD,  60,  100},  // 第三次超快速竖起
    {EAR_COMBO_BOTH_FORWARD,  30,  0}     // 最后调整到居中状态（竖起30ms）
};

const ear_sequence_step_t Tc118sEarController::playful_sequence_[] = {
    // 顽皮：节奏变化，长短时间交替，表达调皮
    {EAR_COMBO_BOTH_FORWARD,  120, 200},  // 慢速竖起120ms，停顿200ms
    {EAR_COMBO_BOTH_BACKWARD, 60,  100},  // 快速下垂60ms，短停顿100ms
    {EAR_COMBO_BOTH_FORWARD,  80,  150},  // 中速竖起80ms，停顿150ms
    {EAR_COMBO_BOTH_BACKWARD, 100, 150},  // 中速下垂100ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD,  60,  100},  // 快速竖起60ms，短停顿100ms
    {EAR_COMBO_BOTH_FORWARD,  40,  0}     // 最后调整到居中状态（竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::sad_sequence_[] = {
    // 悲伤：慢速节奏，长时间动作，表达沉重
    {EAR_COMBO_BOTH_BACKWARD, 200, 500},  // 缓慢下垂200ms，长停顿400ms
    {EAR_COMBO_BOTH_FORWARD,  20, 100},  // 缓慢恢复150ms，停顿200ms
    {EAR_COMBO_BOTH_BACKWARD, 180, 300},  // 再次缓慢下垂180ms，停顿300ms
    {EAR_COMBO_BOTH_FORWARD,  20, 100},  // 缓慢恢复150ms，停顿200ms
    {EAR_COMBO_BOTH_BACKWARD, 180, 200},  // 再次缓慢下垂180ms，停顿300ms
    {EAR_COMBO_BOTH_FORWARD,  30,  0}     // 最后调整到居中状态（竖起60ms）
};

const ear_sequence_step_t Tc118sEarController::surprised_sequence_[] = {
    // 惊讶：快速竖起，然后缓慢下垂，表达"震惊"到"恢复"
    {EAR_COMBO_BOTH_FORWARD,  60,  200},  // 超快速竖起60ms，停顿200ms
    {EAR_COMBO_BOTH_BACKWARD, 180, 100},  // 缓慢下垂180ms，停顿200ms
    {EAR_COMBO_BOTH_FORWARD,  100,  150},  // 中速竖起80ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD,  40,  0}     // 最后调整到居中状态（竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::sleepy_sequence_[] = {
    // 困倦：超慢速节奏，长时间动作，表达疲惫
    {EAR_COMBO_BOTH_BACKWARD, 250, 500},  // 超缓慢下垂250ms，长停顿500ms
    {EAR_COMBO_BOTH_FORWARD,  50,  300},  // 轻微抬起80ms，停顿300ms
    {EAR_COMBO_BOTH_BACKWARD, 200, 400},  // 再次超缓慢下垂200ms，停顿400ms
    {EAR_COMBO_BOTH_FORWARD,  40,  0}     // 最后调整到居中状态（竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::confident_sequence_[] = {
    // 自信：稳定节奏，中等时间动作，表达坚定
    {EAR_COMBO_BOTH_FORWARD,  100, 250},  // 稳定竖起100ms，停顿250ms
    {EAR_COMBO_BOTH_BACKWARD, 100, 250},  // 稳定下垂100ms，停顿250ms
    {EAR_COMBO_BOTH_FORWARD,  100, 250},  // 再次稳定竖起
    {EAR_COMBO_BOTH_FORWARD,  50,  0}     // 最后调整到居中状态（竖起50ms）
};

const ear_sequence_step_t Tc118sEarController::confused_sequence_[] = {
    // 困惑：不规则节奏，时间变化，表达混乱
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, 120, 200},  // 左耳竖起，右耳下垂120ms，停顿200ms
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, 100, 150},  // 左耳竖起，右耳下垂100ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD,  80,  100},  // 双耳竖起80ms，短停顿100ms
    {EAR_COMBO_BOTH_BACKWARD, 100, 150},  // 双耳下垂100ms，停顿150ms
    {EAR_COMBO_BOTH_FORWARD,  40,  0}     // 最后调整到居中状态（双耳竖起40ms）
};

const ear_sequence_step_t Tc118sEarController::loving_sequence_[] = {
    // 爱意：温柔节奏，中等时间动作，表达温柔
    {EAR_COMBO_BOTH_FORWARD,  120, 300},  // 温柔竖起120ms，长停顿300ms
    {EAR_COMBO_BOTH_BACKWARD, 100, 200},  // 温柔下垂100ms，停顿200ms
    {EAR_COMBO_BOTH_FORWARD,  110, 250},  // 再次温柔竖起110ms，停顿250ms
    {EAR_COMBO_BOTH_FORWARD,  50,  0}     // 最后调整到居中状态（竖起50ms）
};

const ear_sequence_step_t Tc118sEarController::angry_sequence_[] = {
    // 愤怒：快速节奏，短时间动作，表达激烈
    {EAR_COMBO_BOTH_FORWARD,  70,  80},   // 快速竖起70ms，短停顿80ms
    {EAR_COMBO_BOTH_BACKWARD, 70,  80},   // 快速下垂70ms，短停顿80ms
    {EAR_COMBO_BOTH_FORWARD,  70,  80},   // 再次快速竖起
    {EAR_COMBO_BOTH_BACKWARD, 70,  80},   // 再次快速下垂
    {EAR_COMBO_BOTH_FORWARD,  70,  80},   // 再次快速竖起
    {EAR_COMBO_BOTH_BACKWARD, 70,  80},   // 再次快速下垂
    {EAR_COMBO_BOTH_FORWARD,  70,  80},   // 第三次快速竖起
    {EAR_COMBO_BOTH_FORWARD,  35,  0}     // 最后调整到居中状态（竖起35ms）
};

const ear_sequence_step_t Tc118sEarController::cool_sequence_[] = {
    // 酷：慢速节奏，长时间动作，表达冷静
    {EAR_COMBO_BOTH_FORWARD,  150, 400},  // 缓慢竖起150ms，长停顿400ms
    {EAR_COMBO_BOTH_BACKWARD, 150, 400},  // 缓慢下垂150ms，长停顿400ms
    {EAR_COMBO_BOTH_FORWARD,  150, 400},  // 再次缓慢竖起
    {EAR_COMBO_BOTH_FORWARD,  75,  0}     // 最后调整到居中状态（竖起75ms）
};

// 默认情绪映射 - 基于时间控制的情绪表达
const std::map<std::string, std::vector<ear_sequence_step_t>> Tc118sEarController::default_emotion_mappings_ = {
    // ===== 中性/无动作情绪 =====
    {"neutral", {}},      // 中性：无动作
    {"relaxed", {}},      // 放松：无动作
    
    // ===== 开心类情绪 - 快速节奏 =====
    {"happy", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}},
    {"joyful", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}},  // 快乐：用开心序列
    {"cheerful", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}}, // 愉快：用开心序列
    
    // ===== 兴奋类情绪 - 超快速节奏 =====
    {"excited", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},
    {"laughing", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},  // 大笑：用兴奋序列
    {"delicious", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}}, // 美味：用兴奋序列
    {"thrilled", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}}, // 兴奋：用兴奋序列
    
    // ===== 顽皮类情绪 - 节奏变化 =====
    {"playful", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},
    {"funny", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},     // 有趣：用顽皮序列
    {"silly", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},    // 傻傻的：用顽皮序列
    {"winking", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + 2)}},  // 眨眼：用顽皮序列前2步
    
    // ===== 悲伤类情绪 - 慢速节奏 =====
    {"sad", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},
    {"crying", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},           // 哭泣：用悲伤序列
    {"embarrassed", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},     // 尴尬：用悲伤序列
    {"disappointed", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},   // 失望：用悲伤序列
    
    // ===== 惊讶类情绪 - 快速到慢速 =====
    {"surprised", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}},
    {"shocked", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}}, // 震惊：用惊讶序列
    {"amazed", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}}, // 惊讶：用惊讶序列
    
    // ===== 愤怒类情绪 - 快速节奏 =====
    {"angry", {std::vector<ear_sequence_step_t>(angry_sequence_, angry_sequence_ + sizeof(angry_sequence_)/sizeof(angry_sequence_[0]))}},
    {"furious", {std::vector<ear_sequence_step_t>(angry_sequence_, angry_sequence_ + sizeof(angry_sequence_)/sizeof(angry_sequence_[0]))}},     // 愤怒：用愤怒序列
    {"annoyed", {std::vector<ear_sequence_step_t>(angry_sequence_, angry_sequence_ + sizeof(angry_sequence_)/sizeof(angry_sequence_[0]))}},   // 烦恼：用愤怒序列
    
    // ===== 好奇类情绪 - 左右交替 =====
    {"curious", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},
    {"thinking", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},  // 思考：用好奇序列
    {"listening", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}}, // 倾听：用好奇序列
    
    // ===== 爱意类情绪 - 温柔节奏 =====
    {"loving", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},
    {"kissy", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},     // 亲吻：用爱意序列
    {"caring", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},   // 关心：用爱意序列
    
    // ===== 自信类情绪 - 稳定节奏 =====
    {"confident", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}},
    {"proud", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}}, // 骄傲：用自信序列
    {"determined", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}}, // 坚定：用自信序列
    
    // ===== 酷类情绪 - 慢速节奏 =====
    {"cool", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},
    {"calm", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},         // 冷静：用酷序列
    {"chill", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},        // 放松：用酷序列
    
    // ===== 困惑类情绪 - 不规则节奏 =====
    {"confused", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}},
    {"puzzled", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}}, // 困惑：用困惑序列
    {"lost", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}},    // 迷失：用困惑序列
    
    // ===== 特殊情绪 - 独特节奏 =====
    {"sleepy", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},     // 困倦：超慢速节奏
    {"tired", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},      // 疲惫：用困倦序列
    {"drowsy", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},     // 昏昏欲睡：用困倦序列
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
    , stop_timer_(nullptr) {
    
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
    
    // 调用基类初始化
    esp_err_t ret = InitializeBase();
    if (ret != ESP_OK) {
        return ret;
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
    
    // 初始化默认情绪映射
    InitializeDefaultEmotionMappings();
    
    // 设置序列模式
    SetupSequencePatterns();
    
    // 创建停止定时器
    stop_timer_ = xTimerCreate("EarStopTimer", pdMS_TO_TICKS(100), pdFALSE, this, 
                               [](TimerHandle_t timer) {
                                   Tc118sEarController* controller = static_cast<Tc118sEarController*>(pvTimerGetTimerID(timer));
                                   controller->OnStopTimer(timer);
                               });
    if (!stop_timer_) {
        ESP_LOGE(TAG, "Failed to create stop timer");
        return ESP_ERR_NO_MEM;
    }
    
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
    
    // 删除停止定时器
    if (stop_timer_) {
        xTimerDelete(stop_timer_, 0);
        stop_timer_ = nullptr;
    }
    
    // 调用基类反初始化
    return DeinitializeBase();
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
    
    // 运行指定时间 - 非阻塞实现
    if (combo.duration_ms > 0) {
        // 启动定时器来在指定时间后停止耳朵
        if (stop_timer_) {
            xTimerStop(stop_timer_, 0);
            xTimerChangePeriod(stop_timer_, pdMS_TO_TICKS(combo.duration_ms), 0);
            xTimerStart(stop_timer_, 0);
        } else {
            // 如果没有定时器，直接停止（避免阻塞）
            StopBoth();
        }
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
    ESP_LOGI(TAG, "StopSequence called: sequence_active=%s, emotion_action_active=%s", 
             sequence_active_ ? "true" : "false", emotion_action_active_ ? "true" : "false");
    
    if (sequence_active_) {
        sequence_active_ = false;
        emotion_action_active_ = false;
        
        if (sequence_timer_) {
            xTimerStop(sequence_timer_, 0);
        }
        
        StopBoth();
        ESP_LOGI(TAG, "Sequence stopped and state reset");
    } else {
        ESP_LOGI(TAG, "No active sequence to stop");
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

void Tc118sEarController::OnSequenceTimer(TimerHandle_t timer) {
    if (!sequence_active_ || current_sequence_.empty()) {
        return;
    }
    
    // 执行当前步骤
    ear_sequence_step_t step = current_sequence_[current_step_index_];
    
    // 执行组合动作 - 简化版本，直接调用MoveBoth
    ear_combo_param_t combo = {step.combo_action, step.duration_ms};
    MoveBoth(combo);
    
    // 移动到下一步
    current_step_index_++;
    
    // 检查序列是否完成
    if (current_step_index_ >= current_sequence_.size()) {
        current_step_index_ = 0;
        current_loop_count_++;
        
        // 检查循环是否完成
        if (current_loop_count_ >= 1) {
            // 序列完成，重置状态
            sequence_active_ = false;
            emotion_action_active_ = false;
            
            ESP_LOGI(TAG, "Sequence completed, resetting emotion state");
            
            // 停止定时器
            if (sequence_timer_) {
                xTimerStop(sequence_timer_, 0);
            }
            
            // 简单设置最终位置
            SetEarFinalPosition();
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
        ESP_LOGW(TAG, "ShouldTriggerEmotion: emotion is null");
        return false;
    }
    
    // 获取当前时间
    uint64_t current_time = esp_timer_get_time() / 1000;
    
    ESP_LOGI(TAG, "ShouldTriggerEmotion: checking %s, current_emotion=%s, emotion_action_active=%s, sequence_active=%s", 
             emotion, current_emotion_.c_str(), emotion_action_active_ ? "true" : "false", sequence_active_ ? "true" : "false");
    
    // 如果当前有序列正在进行，不触发新的情绪
    if (sequence_active_) {
        ESP_LOGI(TAG, "Sequence already active, skipping trigger for %s", emotion);
        return false;
    }
    
    // 如果当前有情绪动作正在进行，不触发新的情绪
    if (emotion_action_active_) {
        ESP_LOGI(TAG, "Emotion action already active, skipping trigger for %s", emotion);
        return false;
    }
    
    // 如果情绪相同且还在冷却期内，不触发
    if (current_emotion_ == emotion && 
        (current_time - last_emotion_time_) < EMOTION_COOLDOWN_MS) {
        ESP_LOGI(TAG, "Emotion %s still in cooldown (%llu ms remaining), skipping trigger", 
                 emotion, EMOTION_COOLDOWN_MS - (current_time - last_emotion_time_));
        return false;
    }
    
    ESP_LOGI(TAG, "ShouldTriggerEmotion: %s is allowed to trigger", emotion);
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
    // 设置耳朵到中立位置（居中），保持激活状态
    ESP_LOGI(TAG, "Setting ears to neutral MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
}

void Tc118sEarController::SetEarInitialPosition() {
    // 系统初始化时设置耳朵到下垂位置
    ESP_LOGI(TAG, "Setting ears to initial DOWN position for system startup");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
}


// 强制重置所有状态 - 用于调试和紧急情况
void Tc118sEarController::ForceResetAllStates() {
    ESP_LOGI(TAG, "ForceResetAllStates: Resetting all ear controller states");
    
    // 停止序列
    StopSequence();
    
    // 重置状态
    current_emotion_ = "neutral";
    last_emotion_time_ = 0;
    
    // 停止所有耳朵
    StopBoth();
    
    // 设置到初始化位置（下垂）
    SetEarInitialPosition();
    
    ESP_LOGI(TAG, "ForceResetAllStates: All states reset successfully");
}

// ===== 新增：基础功能测试方法实现 =====

void Tc118sEarController::TestBasicEarFunctions() {
    ESP_LOGI(TAG, "=== Testing Basic Ear Functions ===");
    
    // 测试单耳控制
    ESP_LOGI(TAG, "Testing LEFT ear FORWARD");
    MoveEar(true, {EAR_ACTION_FORWARD, EAR_POSITION_UP_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing LEFT ear BACKWARD");
    MoveEar(true, {EAR_ACTION_BACKWARD, EAR_POSITION_DOWN_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing RIGHT ear FORWARD");
    MoveEar(false, {EAR_ACTION_FORWARD, EAR_POSITION_UP_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing RIGHT ear BACKWARD");
    MoveEar(false, {EAR_ACTION_BACKWARD, EAR_POSITION_DOWN_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Stopping both ears");
    StopBoth();
    ESP_LOGI(TAG, "=== Basic Functions Test Completed ===");
}

void Tc118sEarController::TestEarPositions() {
    ESP_LOGI(TAG, "=== Testing Ear Positions ===");
    
    // 测试位置控制
    ESP_LOGI(TAG, "Setting both ears to UP position");
    SetEarPosition(true, EAR_POSITION_UP);
    SetEarPosition(false, EAR_POSITION_UP);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "=== Position Test Completed ===");
}

void Tc118sEarController::TestEarCombinations() {
    ESP_LOGI(TAG, "=== Testing Ear Combinations (Simplified) ===");
    
    // 直接使用MoveBoth，避免复杂的组合逻辑
    ESP_LOGI(TAG, "Both ears forward");
    ear_combo_param_t combo = {EAR_COMBO_BOTH_FORWARD, 500};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Both ears backward");
    combo.combo_action = EAR_COMBO_BOTH_BACKWARD;
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Left forward, right backward");
    combo.combo_action = EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD;
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    StopBoth();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "=== Combination Test Completed ===");
}

void Tc118sEarController::TestEarSequences() {
    ESP_LOGI(TAG, "=== Testing Emotion-Triggered Ear Sequences ===");
    
    // 首先强制重置所有状态，确保测试环境干净
    ESP_LOGI(TAG, "Force resetting all states before testing...");
    ForceResetAllStates();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试各种情绪触发
    const char* test_emotions[] = {
        "happy",      // 开心：双耳快速上下摆动
        "excited",    // 兴奋：双耳快速连续摆动
        "curious",    // 好奇：左右耳交替竖起
        "playful",    // 顽皮：多步骤复杂摆动
        "surprised",  // 惊讶：快速竖起后缓慢下垂
        "sad",        // 悲伤：缓慢下垂后快速竖起
        "sleepy"      // 困倦：完全下垂
    };
    
    const int emotion_count = sizeof(test_emotions) / sizeof(test_emotions[0]);
    
    for (int i = 0; i < emotion_count; i++) {
        const char* emotion = test_emotions[i];
        ESP_LOGI(TAG, "Testing emotion: %s", emotion);
        
        // 触发情绪
        esp_err_t ret = TriggerEmotion(emotion);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Emotion '%s' triggered successfully", emotion);
            
            // 等待情绪序列完成
            while (IsSequenceActive()) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // 情绪间间隔
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            ESP_LOGE(TAG, "Failed to trigger emotion '%s': %s", emotion, esp_err_to_name(ret));
        }
    }
    
    // 测试冷却机制
    ESP_LOGI(TAG, "Testing emotion cooldown mechanism...");
    ESP_LOGI(TAG, "Triggering 'happy' emotion twice quickly...");
    
    TriggerEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(100));  // 短暂等待
    TriggerEmotion("happy");  // 应该被冷却机制阻止
    
    // 等待当前序列完成
    while (IsSequenceActive()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 测试停止情绪
    ESP_LOGI(TAG, "Testing emotion stop mechanism...");
    TriggerEmotion("playful");
    vTaskDelay(pdMS_TO_TICKS(500));  // 让序列开始
    StopEmotion();  // 停止当前情绪
    
    // 测试状态重置
    ESP_LOGI(TAG, "Testing state reset mechanism...");
    ForceResetAllStates();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试重置后是否能正常触发
    ESP_LOGI(TAG, "Testing emotion trigger after state reset...");
    TriggerEmotion("happy");
    
    // 等待序列完成
    while (IsSequenceActive()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 重置到默认位置
    ESP_LOGI(TAG, "Resetting ears to default position...");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "=== Emotion-Triggered Sequence Test Completed ===");
}

// 停止定时器回调 - 用于非阻塞的MoveBoth
void Tc118sEarController::OnStopTimer(TimerHandle_t timer) {
    ESP_LOGI(TAG, "Stop timer triggered - stopping both ears");
    StopBoth();
}


