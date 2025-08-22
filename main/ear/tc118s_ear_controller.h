#ifndef _TC118S_EAR_CONTROLLER_H_
#define _TC118S_EAR_CONTROLLER_H_

#include "ear_controller.h"
#include "driver/gpio.h"

// ===== 电机延时参数配置 - 可根据实际电机调整 =====
// 耳朵位置控制延时参数（单位：毫秒）
#define EAR_POSITION_DOWN_TIME_MS      800     // 耳朵下垂所需时间
#define EAR_POSITION_UP_TIME_MS        800     // 耳朵竖起所需时间
#define EAR_POSITION_MIDDLE_TIME_MS    400     // 耳朵到中间位置所需时间

// 情绪动作延时系数（相对于基础延时）
#define EMOTION_QUICK_RATIO            0.3     // 快速动作：30%的基础时间
#define EMOTION_NORMAL_RATIO           0.6     // 正常动作：60%的基础时间
#define EMOTION_SLOW_RATIO             0.8     // 慢速动作：80%的基础时间
#define EMOTION_FULL_RATIO             1.0     // 完整动作：100%的基础时间

// 情绪动作间延时系数
#define EMOTION_GAP_QUICK_RATIO        0.2     // 快速间隔：20%的基础时间
#define EMOTION_GAP_NORMAL_RATIO       0.4     // 正常间隔：40%的基础时间
#define EMOTION_GAP_SLOW_RATIO         0.6     // 慢速间隔：60%的基础时间
#define EMOTION_GAP_FULL_RATIO         1.0     // 完整间隔：100%的基础时间

// 延时计算宏
#define EMOTION_TIME(base_time, ratio)     ((uint32_t)((base_time) * (ratio)))
#define EMOTION_GAP(base_time, ratio)      ((uint32_t)((base_time) * (ratio)))

// 场景动作延时参数
#define SCENARIO_DEFAULT_DELAY_MS      150     // 场景步骤间默认延时
#define SCENARIO_LOOP_DELAY_MS         300     // 场景循环间延时
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间

// ===== 宏定义结束 =====

class Tc118sEarController : public EarController {
public:
    Tc118sEarController(gpio_num_t left_ina_pin, gpio_num_t left_inb_pin,
                       gpio_num_t right_ina_pin, gpio_num_t right_inb_pin);
    virtual ~Tc118sEarController();

    // 实现抽象方法
    virtual esp_err_t Initialize() override;
    virtual esp_err_t Deinitialize() override;
    virtual void SetGpioLevels(bool left_ear, ear_action_t action) override;

    // 重写基础控制接口
    virtual esp_err_t MoveEar(bool left_ear, ear_action_param_t action) override;
    virtual esp_err_t StopEar(bool left_ear) override;
    virtual esp_err_t StopBoth() override;

    // 重写双耳组合控制接口
    virtual esp_err_t MoveBoth(ear_combo_param_t combo) override;

    // 重写位置控制接口
    virtual esp_err_t SetEarPosition(bool left_ear, ear_position_t position) override;
    virtual ear_position_t GetEarPosition(bool left_ear) override;
    virtual esp_err_t ResetToDefault() override;

    // 重写序列控制接口
    virtual esp_err_t PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop = false) override;
    virtual esp_err_t StopSequence() override;

    // 重写情绪控制接口
    virtual esp_err_t SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) override;
    virtual esp_err_t TriggerEmotion(const char* emotion) override;
    virtual esp_err_t StopEmotion() override;

    // 重写状态查询接口
    virtual ear_action_t GetCurrentAction(bool left_ear) override;
    virtual bool IsMoving(bool left_ear) override;
    virtual bool IsSequenceActive() override;

private:
    // GPIO引脚配置
    gpio_num_t left_ina_pin_;
    gpio_num_t left_inb_pin_;
    gpio_num_t right_ina_pin_;
    gpio_num_t right_inb_pin_;

    // 情绪状态跟踪
    std::string current_emotion_;
    uint64_t last_emotion_time_;
    bool emotion_action_active_;
    
    // 耳朵位置状态跟踪
    ear_position_t left_ear_position_;
    ear_position_t right_ear_position_;

    // 私有方法
    void InitializeDefaultEmotionMappings();
    void SetupSequencePatterns();
    static void SequenceTimerCallbackWrapper(TimerHandle_t timer);
    void InternalSequenceTimerCallback(TimerHandle_t timer);
    bool ShouldTriggerEmotion(const char* emotion);
    void UpdateEmotionState(const char* emotion);
    void SetEarFinalPosition();

    // 默认情绪序列定义 - 使用新的序列结构
    static const ear_sequence_step_t happy_sequence_[];
    static const ear_sequence_step_t curious_sequence_[];
    static const ear_sequence_step_t excited_sequence_[];
    static const ear_sequence_step_t playful_sequence_[];
    static const ear_sequence_step_t sad_sequence_[];
    static const ear_sequence_step_t surprised_sequence_[];
    static const ear_sequence_step_t sleepy_sequence_[];

    // 默认情绪映射
    static const std::map<std::string, std::vector<ear_sequence_step_t>> default_emotion_mappings_;
};

#endif // _TC118S_EAR_CONTROLLER_H_
