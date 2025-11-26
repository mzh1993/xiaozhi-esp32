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

// ===== 核心动作定义 =====
// 单耳基础动作 - 最基本的物理控制
typedef enum {
    EAR_ACTION_STOP = 0,           // 停止
    EAR_ACTION_FORWARD = 1,        // 向前摆动
    EAR_ACTION_BACKWARD = 2,       // 向后摆动
    EAR_ACTION_BRAKE = 3           // 刹车
} ear_action_t;

// 双耳组合动作 - 预定义的常用组合
typedef enum {
    // 对称动作（最常用）
    EAR_COMBO_BOTH_FORWARD = 0,        // 双耳向前
    EAR_COMBO_BOTH_BACKWARD = 1,       // 双耳向后
    EAR_COMBO_BOTH_STOP = 2,           // 双耳停止
    
    // 单耳动作
    EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD = 3,    // 左耳向前，右耳保持
    EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD = 4,    // 左耳保持，右耳向前
    
    // 交叉动作
    EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD = 5,  // 左耳向前，右耳向后
    EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD = 6,  // 左耳向后，右耳向前
    
    // 总数
    EAR_COMBO_COUNT = 7
} ear_combo_action_t;

// 耳朵位置状态
typedef enum {
    EAR_POSITION_DOWN = 0,      // 耳朵下垂（默认状态）
    EAR_POSITION_UP = 1,        // 耳朵竖起
    EAR_POSITION_MIDDLE = 2     // 耳朵中间位置
} ear_position_t;

// ===== 数据结构 =====
// 单耳动作参数
typedef struct {
    ear_action_t action;
    uint32_t duration_ms;          // 运行时间（毫秒）
} ear_action_param_t;

// 双耳组合动作参数
typedef struct {
    ear_combo_action_t combo_action;  // 组合动作类型
    uint32_t duration_ms;             // 运行时间（毫秒）
} ear_combo_param_t;

// 动作序列步骤（用于复杂场景）
typedef struct {
    ear_combo_action_t combo_action;  // 使用组合动作
    uint32_t duration_ms;             // 运行时间
    uint32_t delay_ms;                // 动作间隔
} ear_sequence_step_t;

#ifdef __cplusplus
}
#endif

// C++ 抽象基类
#ifdef __cplusplus

#include <string>
#include <map>
#include <vector>

// 耳朵控制结构
typedef struct {
    gpio_num_t ina_pin;
    gpio_num_t inb_pin;
    bool is_left_ear;
    ear_action_t current_action;
    bool is_active;
} ear_control_t;

class EarController {
public:
    EarController();
    virtual ~EarController();

    // ===== 核心控制接口 - 简单易用 =====
    
    // 1. 单耳控制（基础功能）
    virtual esp_err_t MoveEar(bool left_ear, ear_action_param_t action);  // 移除 = 0，提供默认实现
    virtual esp_err_t StopEar(bool left_ear);  // 移除 = 0，提供默认实现
    virtual esp_err_t StopBoth();  // 移除 = 0，提供默认实现
    
    // 2. 双耳组合控制（常用功能）
    virtual esp_err_t MoveBoth(ear_combo_param_t combo);  // 移除 = 0，提供默认实现
    
    // 3. 位置控制（高级功能）
    virtual esp_err_t SetEarPosition(bool left_ear, ear_position_t position) = 0;
    virtual ear_position_t GetEarPosition(bool left_ear) = 0;
    virtual esp_err_t ResetToDefault() = 0;
    virtual void SetEarInitialPosition() = 0;
    
    // 4. 场景控制（复杂功能）
    virtual esp_err_t PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop = false) = 0;
    virtual esp_err_t StopSequence() = 0;
    
    // 5. 情绪触发（应用功能）
    virtual esp_err_t SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) = 0;
    virtual esp_err_t TriggerEmotion(const char* emotion) = 0;
    virtual esp_err_t StopEmotion() = 0;
    
    // ===== 状态查询接口 =====
    virtual ear_action_t GetCurrentAction(bool left_ear) = 0;
    virtual bool IsMoving(bool left_ear) = 0;
    virtual bool IsSequenceActive() = 0;
    
    // ===== 序列完成回调接口 =====
    // P0修复：用于Worker在最后一个序列任务完成后标记序列完成
    virtual void MarkSequenceCompleted() = 0;
    // P0修复：标记当前MoveBoth是否是序列的最后一个步骤
    virtual void SetLastSequenceMoveFlag(bool is_last) = 0;
    
    // ===== 测试接口 - 用于调试和功能验证 =====
    virtual void TestBasicEarFunctions() = 0;
    virtual void TestEarPositions() = 0;
    virtual void TestEarCombinations() = 0;
    virtual void TestEarSequences() = 0;
    
    // ===== 初始化和反初始化接口 =====
    virtual esp_err_t Initialize() = 0;
    virtual esp_err_t Deinitialize() = 0;
    
    // 基类提供的通用初始化方法
    virtual esp_err_t InitializeBase();
    virtual esp_err_t DeinitializeBase();

protected:
    // 子类需要实现的抽象方法
    virtual void SetGpioLevels(bool left_ear, ear_action_t action) = 0;
    
    // 通用功能方法
    virtual void SequenceTimerCallback(TimerHandle_t timer);
    
    // 子类可以重写的定时器回调方法
    virtual void OnSequenceTimer(TimerHandle_t timer);
    
    // 静态回调包装函数，用于定时器
    static void StaticSequenceTimerCallback(TimerHandle_t timer);
    
    // 内部状态
    ear_control_t left_ear_;
    ear_control_t right_ear_;
    bool sequence_active_;
    TimerHandle_t sequence_timer_;
    std::vector<ear_sequence_step_t> current_sequence_;
    uint8_t current_step_index_;
    uint8_t current_loop_count_;
    std::map<std::string, std::vector<ear_sequence_step_t>> emotion_mappings_;
    bool initialized_;
    
    // 耳朵位置状态跟踪 - 基类统一管理
    ear_position_t left_ear_position_;
    ear_position_t right_ear_position_;
};

#endif // __cplusplus

#endif // _EAR_CONTROLLER_H_
