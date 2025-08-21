#ifndef _EAR_CONTROLLER_H_
#define _EAR_CONTROLLER_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ear Direction Enum
typedef enum {
    EAR_STOP = 0,           // 停止
    EAR_FORWARD = 1,        // 向前摆动
    EAR_BACKWARD = 2,       // 向后摆动
    EAR_BRAKE = 3           // 刹车
} ear_direction_t;

// Ear Speed Enum
typedef enum {
    EAR_SPEED_SLOW = 1,     // 慢速
    EAR_SPEED_NORMAL = 2,   // 正常速度
    EAR_SPEED_FAST = 3,     // 快速
    EAR_SPEED_VERY_FAST = 4 // 极快速度
} ear_speed_t;

// Ear Scenario Enum
typedef enum {
    EAR_SCENARIO_NORMAL = 0,        // 正常状态
    EAR_SCENARIO_PEEKABOO = 1,      // 躲猫猫 - 双耳长时间向前
    EAR_SCENARIO_INSECT_BITE = 2,   // 蚊虫叮咬 - 单边快速摆动
    EAR_SCENARIO_CURIOUS = 3,       // 好奇 - 双耳交替摆动
    EAR_SCENARIO_SLEEPY = 4,        // 困倦 - 缓慢下垂
    EAR_SCENARIO_EXCITED = 5,       // 兴奋 - 快速摆动
    EAR_SCENARIO_SAD = 6,           // 伤心 - 耳朵下垂
    EAR_SCENARIO_ALERT = 7,         // 警觉 - 耳朵竖起
    EAR_SCENARIO_PLAYFUL = 8,       // 玩耍 - 不规则摆动
    EAR_SCENARIO_CUSTOM = 9         // 自定义模式
} ear_scenario_t;

#ifdef __cplusplus
}
#endif

// C++ 抽象基类
#ifdef __cplusplus

#include <string>
#include <map>

// Ear Control Structure
typedef struct {
    gpio_num_t ina_pin;
    gpio_num_t inb_pin;
    bool is_left_ear;
    ear_direction_t current_direction;
    ear_speed_t current_speed;
    bool is_active;
} ear_control_t;

// Ear Movement Pattern Structure
typedef struct {
    ear_direction_t direction;
    ear_speed_t speed;
    uint32_t duration_ms;
    uint32_t delay_ms;
} ear_movement_step_t;

// Ear Scenario Configuration
typedef struct {
    ear_scenario_t scenario;
    ear_movement_step_t *steps;
    uint8_t step_count;
    bool loop_enabled;
    uint8_t loop_count;
} ear_scenario_config_t;

// 情绪到耳朵动作的映射结构
typedef struct {
    ear_scenario_t ear_scenario;
    uint32_t duration_ms;
    bool auto_stop;
} emotion_ear_mapping_t;

class EarController {
public:
    EarController();
    virtual ~EarController();

    // 基础控制接口
    virtual esp_err_t SetDirection(bool left_ear, ear_direction_t direction) = 0;
    virtual esp_err_t SetSpeed(bool left_ear, ear_speed_t speed) = 0;
    virtual esp_err_t Stop(bool left_ear) = 0;
    virtual esp_err_t StopBoth() = 0;

    // 高级控制接口
    virtual esp_err_t MoveTimed(bool left_ear, ear_direction_t direction, 
                               ear_speed_t speed, uint32_t duration_ms) = 0;
    virtual esp_err_t MoveBothTimed(ear_direction_t direction, 
                                   ear_speed_t speed, uint32_t duration_ms) = 0;

    // 场景控制接口
    virtual esp_err_t PlayScenario(ear_scenario_t scenario) = 0;
    virtual esp_err_t PlayScenarioAsync(ear_scenario_t scenario) = 0;
    virtual esp_err_t StopScenario() = 0;

    // 特定场景接口
    virtual esp_err_t PeekabooMode(uint32_t duration_ms) = 0;
    virtual esp_err_t InsectBiteMode(bool left_ear, uint32_t duration_ms) = 0;
    virtual esp_err_t CuriousMode(uint32_t duration_ms) = 0;
    virtual esp_err_t SleepyMode() = 0;
    virtual esp_err_t ExcitedMode(uint32_t duration_ms) = 0;
    virtual esp_err_t SadMode() = 0;
    virtual esp_err_t AlertMode() = 0;
    virtual esp_err_t PlayfulMode(uint32_t duration_ms) = 0;

    // 自定义模式接口
    virtual esp_err_t PlayCustomPattern(ear_movement_step_t *steps, 
                                       uint8_t step_count, bool loop) = 0;
    virtual esp_err_t SetCustomScenario(ear_scenario_config_t *config) = 0;

    // 情绪集成接口
    virtual esp_err_t TriggerByEmotion(const char* emotion) = 0;
    virtual esp_err_t SetEmotionMapping(const char* emotion, ear_scenario_t scenario, 
                                       uint32_t duration_ms) = 0;
    virtual esp_err_t GetEmotionMapping(const char* emotion, emotion_ear_mapping_t* mapping) = 0;
    virtual esp_err_t StopEmotionAction() = 0;

    // 状态查询接口
    virtual ear_direction_t GetCurrentDirection(bool left_ear) = 0;
    virtual ear_speed_t GetCurrentSpeed(bool left_ear) = 0;
    virtual bool IsMoving(bool left_ear) = 0;
    virtual bool IsScenarioActive() = 0;

    // 高级情绪功能
    virtual esp_err_t TriggerByEmotionWithIntensity(const char* emotion, float intensity) = 0;
    virtual esp_err_t TransitionEmotion(const char* from_emotion, const char* to_emotion, 
                                       uint32_t transition_time_ms) = 0;

    // 初始化和反初始化接口
    virtual esp_err_t Initialize() = 0;
    virtual esp_err_t Deinitialize() = 0;

protected:
    // 子类需要实现的抽象方法
    virtual void SetGpioLevels(bool left_ear, ear_direction_t direction) = 0;

    // 通用功能方法
    virtual uint32_t SpeedToDelay(ear_speed_t speed);
    virtual void ApplySpeedControl(bool left_ear, ear_speed_t speed);
    virtual void ScenarioTimerCallback(TimerHandle_t timer);

    // 内部状态
    ear_control_t left_ear_;
    ear_control_t right_ear_;
    bool scenario_active_;
    TimerHandle_t scenario_timer_;
    ear_scenario_config_t current_scenario_;
    uint8_t current_step_index_;
    uint8_t current_loop_count_;
    std::map<std::string, emotion_ear_mapping_t> emotion_mappings_;
    bool initialized_;
};

#endif // __cplusplus

#endif // _EAR_CONTROLLER_H_
