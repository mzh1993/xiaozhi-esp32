#ifndef _TC118S_EAR_CONTROLLER_H_
#define _TC118S_EAR_CONTROLLER_H_

#include "ear_controller.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ===== 电机延时参数配置 - 基于实际测试优化 =====
// 耳朵位置控制延时参数（单位：毫秒）
#define EAR_POSITION_DOWN_TIME_MS      50     // 耳朵从竖起到下垂所需时间
#define EAR_POSITION_UP_TIME_MS        50     // 耳朵从下垂到竖起所需时间
#define EAR_POSITION_MIDDLE_TIME_MS    40      // 耳朵从竖起最高点回到中间位置所需时间（从20增加到40，确保动作完整）

// 场景动作延时参数
#define SCENARIO_DEFAULT_DELAY_MS      100     // 场景步骤间默认延时（增加）
#define SCENARIO_LOOP_DELAY_MS         250     // 场景循环间延时（增加）
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间

// ===== 宏定义结束 =====

// ===== 统一的动作/停顿时间预设（毫秒）便于全局调优（聚合版） =====
// 动作时长（建议优先使用这几档）
#define EAR_MOVE_TINY_MS            35
#define EAR_MOVE_ADJUST_MS          45
#define EAR_MOVE_SHORT_MS           150
#define EAR_MOVE_FAST_MS            55
#define EAR_MOVE_QUICK_MS           65
#define EAR_MOVE_MEDIUM_MS          75
#define EAR_MOVE_SLOW_MS            100
#define EAR_MOVE_SLOW_PLUS_MS       120
#define EAR_MOVE_LONG_MS            150

// 停顿时长（建议优先使用这几档）
#define EAR_PAUSE_NONE_MS           0
#define EAR_PAUSE_SHORT_MS          100
#define EAR_PAUSE_MEDIUM_MS         150
#define EAR_PAUSE_LONG_MS           200
#define EAR_PAUSE_XLONG_MS          300
#define EAR_PAUSE_XXLONG_MS         500

// 软启动/错峰控制（供上层引用）
#define EAR_POWER_ON_STABILIZE_MS   150   // 板级已实现电源稳定等待（参考值）
#define EAR_START_STAGGER_MS        60    // 双耳启动错峰间隔（毫秒），建议 60–120 之间
// 触发限频/最小时长
#define EAR_MOVE_COOLDOWN_MS        80
#define EAR_BOTH_MIN_DURATION_MS    50      // 最小双耳动作时长（从30增加到50，确保动作完整）

// 软启动开关与参数（默认关闭，后续需要可开启并完善 PWM）
#define EAR_SOFTSTART_ENABLE        0     // 0: 关闭；1: 开启
#define EAR_SOFTSTART_TIME_MS       200   // 软启动总时长（毫秒）
#define EAR_SOFTSTART_STEPS         8     // 软启动步数

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
    
    // 重写定时器回调方法
    virtual void OnSequenceTimer(TimerHandle_t timer) override;
    
    // 新增：基础功能测试方法
    virtual void TestBasicEarFunctions() override;
    virtual void TestEarPositions() override;
    virtual void TestEarCombinations() override;
    virtual void TestEarSequences() override;
    
    // 调试和紧急情况方法
    void ForceResetAllStates();
    
    // 系统初始化方法
    void SetEarInitialPosition();

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
    
    // 停止定时器 - 用于非阻塞的MoveBoth
    TimerHandle_t stop_timer_;
    
    // 耳朵位置状态跟踪 - 已从基类继承，无需重复声明

    // 私有方法
    void InitializeDefaultEmotionMappings();
    void SetupSequencePatterns();
    bool ShouldTriggerEmotion(const char* emotion);
    void UpdateEmotionState(const char* emotion);
    void SetEarFinalPosition();
    void OnStopTimer(TimerHandle_t timer);

    // 启动策略与软启动
    void SoftStartSingleEar(bool left_ear, ear_action_t action);
    void StartBothWithStagger(ear_combo_action_t combo_action, uint32_t duration_ms);
    
    // 延迟位置设置（避免阻塞定时器回调）
    void ScheduleEarFinalPosition();


    // 并发控制
    SemaphoreHandle_t state_mutex_ = nullptr;
    volatile bool moving_both_ = false;
    uint64_t last_move_tick_ms_ = 0;

    // 默认情绪序列定义 - 基于时间控制的情绪表达
    static const ear_sequence_step_t happy_sequence_[];
    static const ear_sequence_step_t curious_sequence_[];
    static const ear_sequence_step_t excited_sequence_[];
    static const ear_sequence_step_t playful_sequence_[];
    static const ear_sequence_step_t sad_sequence_[];
    static const ear_sequence_step_t surprised_sequence_[];
    static const ear_sequence_step_t sleepy_sequence_[];
    static const ear_sequence_step_t confident_sequence_[];
    static const ear_sequence_step_t confused_sequence_[];
    static const ear_sequence_step_t loving_sequence_[];
    static const ear_sequence_step_t angry_sequence_[];
    static const ear_sequence_step_t cool_sequence_[];

    // 默认情绪映射
    static const std::map<std::string, std::vector<ear_sequence_step_t>> default_emotion_mappings_;
};

#endif // _TC118S_EAR_CONTROLLER_H_
