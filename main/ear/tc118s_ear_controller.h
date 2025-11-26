#ifndef _TC118S_EAR_CONTROLLER_H_
#define _TC118S_EAR_CONTROLLER_H_

#include "ear_controller.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ===== 电机延时参数配置 - 基于实际测试优化 =====
// 耳朵位置控制延时参数（单位：毫秒）
#define EAR_POSITION_DOWN_TIME_MS      120     // 耳朵从竖起到下垂所需时间
#define EAR_POSITION_UP_TIME_MS        120     // 耳朵从下垂到竖起所需时间
#define EAR_POSITION_MIDDLE_TIME_MS    60      // 耳朵从竖起最高点回到中间位置所需时间（60ms，确保动作完整）

// 场景动作延时参数
#define SCENARIO_DEFAULT_DELAY_MS      100     // 场景步骤间默认延时（增加）
#define SCENARIO_LOOP_DELAY_MS         250     // 场景循环间延时（增加）
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间

// ===== 宏定义结束 =====

// ===== 简化的动作/停顿时间预设（毫秒）- 基于三个基础参数 =====
// 动作时长：基于位置控制参数，简化设计
// - 小幅动作（快速摇摆）：30ms（约为 MIDDLE 的 2/3）
// - 中幅动作（正常摇摆）：60ms（等于 MIDDLE，中间位置）
// - 大幅动作（完全竖起/下垂）：120ms（等于 UP/DOWN，完全位置）
#define EAR_MOVE_SMALL_MS           30   // 小幅动作，用于快速摇摆（兴奋）
#define EAR_MOVE_MID_MS             60   // 中幅动作，基于 MIDDLE 参数
#define EAR_MOVE_FULL_MS            120  // 大幅动作，基于 UP/DOWN 参数

// 停顿时长：简化设计
#define EAR_PAUSE_NONE_MS           0    // 无停顿
#define EAR_PAUSE_SHORT_MS          80   // 短停顿（快速摇摆时用）
#define EAR_PAUSE_MEDIUM_MS         150  // 中等停顿（正常摇摆时用）
#define EAR_PAUSE_LONG_MS           300  // 长停顿（情绪转换时用）
#define EAR_PAUSE_VERY_LONG_MS      600  // 非常长停顿（坏情绪时用）

// 软启动/错峰控制（供上层引用）
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
    
    // P0修复：重写基类的序列完成回调方法
    virtual void MarkSequenceCompleted() override;
    // P0修复：标记当前MoveBoth是否是序列的最后一个步骤
    virtual void SetLastSequenceMoveFlag(bool is_last) override;

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
    // 单耳停止定时器与上下文
    TimerHandle_t stop_timer_left_ = nullptr;
    TimerHandle_t stop_timer_right_ = nullptr;
    struct StopCtx { Tc118sEarController* self; bool left; };
    StopCtx* stop_ctx_left_ = nullptr;
    StopCtx* stop_ctx_right_ = nullptr;
    
    // 耳朵位置状态跟踪 - 已从基类继承，无需重复声明

    // 私有方法
    void InitializeDefaultEmotionMappings();
    void SetupSequencePatterns();
    bool ShouldTriggerEmotion(const char* emotion);
    void UpdateEmotionState(const char* emotion);
    void SetEarFinalPosition();
    void OnStopTimer(TimerHandle_t timer);
    void OnSingleStopTimer(TimerHandle_t timer);

    // 启动策略与软启动
    void SoftStartSingleEar(bool left_ear, ear_action_t action);
    void StartBothWithStagger(ear_combo_action_t combo_action, uint32_t duration_ms);
    
    // 延迟位置设置（避免阻塞定时器回调）
    void ScheduleEarFinalPosition();

    // 并发控制
    SemaphoreHandle_t state_mutex_ = nullptr;
    volatile bool moving_both_ = false;
    ear_combo_action_t current_combo_action_ = EAR_COMBO_BOTH_STOP;
    uint64_t last_combo_start_time_ms_ = 0;
    uint64_t last_move_tick_ms_ = 0;
    // 持续时间监控：记录动作的实际执行时间
    uint64_t gpio_set_time_ms_ = 0;           // GPIO 实际设置时间（精确）
    uint64_t scheduled_duration_ms_ = 0;      // 计划持续时间
    uint64_t stop_timer_scheduled_time_ms_ = 0; // 停止定时器启动时间
    // P0修复：标记当前MoveBoth是否是序列的最后一个步骤
    volatile bool is_last_sequence_move_ = false;

    void UpdateComboState(bool moving, ear_combo_action_t action, uint64_t timestamp_ms);
    void ResetComboState();
    void ScheduleComboStop(uint32_t duration_ms);

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
    
    // 不同强度的情绪序列 - 基于基础序列优化
    static const ear_sequence_step_t crying_sequence_[];      // 哭泣：比悲伤更强烈
    static const ear_sequence_step_t furious_sequence_[];     // 狂怒：比愤怒更激烈
    static const ear_sequence_step_t shocked_sequence_[];     // 震惊：比惊讶更强烈
    static const ear_sequence_step_t annoyed_sequence_[];     // 烦恼：比愤怒更温和
    static const ear_sequence_step_t embarrassed_sequence_[]; // 尴尬：短暂快速下垂
    static const ear_sequence_step_t thinking_sequence_[];    // 思考：比好奇更慢更稳定
    static const ear_sequence_step_t listening_sequence_[];   // 倾听：单耳交替，专注

    // 默认情绪映射
    static const std::map<std::string, std::vector<ear_sequence_step_t>> default_emotion_mappings_;
};

#endif // _TC118S_EAR_CONTROLLER_H_
