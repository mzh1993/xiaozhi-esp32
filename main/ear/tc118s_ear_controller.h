#ifndef _TC118S_EAR_CONTROLLER_H_
#define _TC118S_EAR_CONTROLLER_H_

#include "ear_controller.h"
#include "driver/gpio.h"

// ===== 电机延时参数配置 - 可根据实际电机调整 =====
// 耳朵位置控制延时参数（单位：毫秒）
#define EAR_POSITION_DOWN_TIME_MS      800     // 耳朵下垂所需时间
#define EAR_POSITION_UP_TIME_MS        800     // 耳朵竖起所需时间
#define EAR_POSITION_MIDDLE_TIME_MS    400     // 耳朵到中间位置所需时间

// 场景动作延时参数
#define SCENARIO_DEFAULT_DELAY_MS      150     // 场景步骤间默认延时
#define SCENARIO_LOOP_DELAY_MS         300     // 场景循环间延时
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间

// 速度控制延时参数
#define SPEED_SLOW_DELAY_MS            50      // 慢速延时
#define SPEED_NORMAL_DELAY_MS          20      // 正常速度延时
#define SPEED_FAST_DELAY_MS            10      // 快速延时
#define SPEED_VERY_FAST_DELAY_MS       5       // 极快延时

// 特殊动作延时参数
#define PEEKABOO_DURATION_MS           2000    // 躲猫猫模式持续时间
#define INSECT_BITE_STEP_TIME_MS       150     // 蚊虫叮咬单步时间
#define INSECT_BITE_DELAY_MS           100     // 蚊虫叮咬步骤间延时
#define CURIOUS_STEP_TIME_MS           800     // 好奇模式单步时间
#define CURIOUS_DELAY_MS               400     // 好奇模式步骤间延时
#define EXCITED_STEP_TIME_MS           300     // 兴奋模式单步时间
#define EXCITED_DELAY_MS               200     // 兴奋模式步骤间延时
#define PLAYFUL_STEP_TIME_MS           600     // 玩耍模式单步时间
#define PLAYFUL_DELAY_MS               300     // 玩耍模式步骤间延时

// ===== 宏定义结束 =====

class Tc118sEarController : public EarController {
public:
    Tc118sEarController(gpio_num_t left_ina_pin, gpio_num_t left_inb_pin,
                       gpio_num_t right_ina_pin, gpio_num_t right_inb_pin);
    virtual ~Tc118sEarController();

    // 实现抽象方法
    virtual esp_err_t Initialize() override;
    virtual esp_err_t Deinitialize() override;
    virtual void SetGpioLevels(bool left_ear, ear_direction_t direction) override;

    // 重写基础控制接口
    virtual esp_err_t SetDirection(bool left_ear, ear_direction_t direction) override;
    virtual esp_err_t SetSpeed(bool left_ear, ear_speed_t speed) override;
    virtual esp_err_t Stop(bool left_ear) override;
    virtual esp_err_t StopBoth() override;
    virtual esp_err_t MoveTimed(bool left_ear, ear_direction_t direction, 
                               ear_speed_t speed, uint32_t duration_ms) override;
    virtual esp_err_t MoveBothTimed(ear_direction_t direction, 
                                   ear_speed_t speed, uint32_t duration_ms) override;

    // 重写场景控制接口
    virtual esp_err_t PlayScenario(ear_scenario_t scenario) override;
    virtual esp_err_t PlayScenarioAsync(ear_scenario_t scenario) override;
    virtual esp_err_t StopScenario() override;

    // 重写特定场景接口
    virtual esp_err_t PeekabooMode(uint32_t duration_ms) override;
    virtual esp_err_t InsectBiteMode(bool left_ear, uint32_t duration_ms) override;
    virtual esp_err_t CuriousMode(uint32_t duration_ms) override;
    virtual esp_err_t SleepyMode() override;
    virtual esp_err_t ExcitedMode(uint32_t duration_ms) override;
    virtual esp_err_t SadMode() override;
    virtual esp_err_t AlertMode() override;
    virtual esp_err_t PlayfulMode(uint32_t duration_ms) override;

    // 重写自定义模式接口
    virtual esp_err_t PlayCustomPattern(ear_movement_step_t *steps, 
                                       uint8_t step_count, bool loop) override;
    virtual esp_err_t SetCustomScenario(ear_scenario_config_t *config) override;

    // 重写情绪集成接口
    virtual esp_err_t TriggerByEmotion(const char* emotion) override;
    virtual esp_err_t SetEmotionMapping(const char* emotion, ear_scenario_t scenario, 
                                       uint32_t duration_ms) override;
    virtual esp_err_t GetEmotionMapping(const char* emotion, emotion_ear_mapping_t* mapping) override;
    virtual esp_err_t StopEmotionAction() override;

    // 重写状态查询接口
    virtual ear_direction_t GetCurrentDirection(bool left_ear) override;
    virtual ear_speed_t GetCurrentSpeed(bool left_ear) override;
    virtual bool IsMoving(bool left_ear) override;
    virtual bool IsScenarioActive() override;
    
    // 重写耳朵位置状态管理接口 - 新增
    virtual ear_position_t GetEarPosition(bool left_ear) override;
    virtual esp_err_t SetEarPosition(bool left_ear, ear_position_t position) override;
    virtual esp_err_t ResetEarsToDefaultPosition() override;
    virtual esp_err_t EnsureEarsDown() override;

    // 重写高级情绪功能
    virtual esp_err_t TriggerByEmotionWithIntensity(const char* emotion, float intensity) override;
    virtual esp_err_t TransitionEmotion(const char* from_emotion, const char* to_emotion, 
                                       uint32_t transition_time_ms) override;

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
    
    // 耳朵位置状态跟踪 - 新增
    ear_position_t left_ear_position_;
    ear_position_t right_ear_position_;
    ear_position_t target_left_ear_position_;
    ear_position_t target_right_ear_position_;

    // 私有方法
    void InitializeDefaultEmotionMappings();
    void SetupScenarioPatterns();
    static void ScenarioTimerCallbackWrapper(TimerHandle_t timer);
    void InternalScenarioTimerCallback(TimerHandle_t timer);
    bool ShouldTriggerEmotion(const char* emotion);
    void UpdateEmotionState(const char* emotion);
    void SetEarFinalPosition();

    // 场景模式定义
    static ear_movement_step_t peekaboo_steps_[];
    static ear_movement_step_t insect_bite_steps_[];
    static ear_movement_step_t curious_steps_[];
    static ear_movement_step_t excited_steps_[];
    static ear_movement_step_t playful_steps_[];

    // 新增：更自然的情绪场景
    static ear_movement_step_t gentle_happy_steps_[];
    static ear_movement_step_t surprised_steps_[];
    static ear_movement_step_t sleepy_steps_[];
    static ear_movement_step_t sad_steps_[];

    // 默认情绪映射
    static const std::map<std::string, emotion_ear_mapping_t> default_emotion_mappings_;
};

#endif // _TC118S_EAR_CONTROLLER_H_
