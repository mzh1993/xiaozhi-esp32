#ifndef _TC118S_EAR_CONTROLLER_H_
#define _TC118S_EAR_CONTROLLER_H_

#include "ear_controller.h"
#include "driver/gpio.h"

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

    // 私有方法
    void InitializeDefaultEmotionMappings();
    void SetupScenarioPatterns();
    static void ScenarioTimerCallbackWrapper(TimerHandle_t timer);
    void InternalScenarioTimerCallback(TimerHandle_t timer);

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
