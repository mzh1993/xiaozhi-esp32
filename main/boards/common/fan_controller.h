#ifndef __FAN_CONTROLLER_H__
#define __FAN_CONTROLLER_H__

#include "mcp_server.h"
#include "led188_display.h"
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <mutex>
#include <atomic>
#include <string>

// 风扇控制模式
enum class FanControlMode {
    OFFLINE = 0,    // 离线模式：触摸按钮控制
    ONLINE = 1      // 在线模式：语音控制
};

// 风扇控制命令 - 简化为统一接口
enum class FanCommand {
    TURN_OFF,           // 关闭风扇
    SET_PERCENTAGE,     // 设置百分比 (0-100)
    NEXT_LEVEL,         // 下一个档位 (按键专用)
    EMERGENCY_STOP      // 紧急停止
};

// 风扇控制请求结构 - 简化
struct FanControlRequest {
    FanCommand command;
    uint8_t percentage;  // 0-100，用于百分比控制
    bool from_voice;     // 是否来自语音命令
};

class FanController {
private:
    // 硬件控制
    gpio_num_t button_gpio_;      // 轻触按键GPIO
    gpio_num_t pwm_gpio_;         // PWM控制GPIO
    ledc_channel_t pwm_channel_;  // PWM通道
    
    // 状态管理 - 统一为百分比控制
    std::atomic<bool> power_{false};
    std::atomic<uint8_t> current_percentage_{0};          // 当前百分比 (0-100)
    std::atomic<FanControlMode> control_mode_{FanControlMode::OFFLINE};
    
    // 控制队列和任务
    QueueHandle_t command_queue_;
    TaskHandle_t control_task_;
    TaskHandle_t button_task_;    // 按键检测任务
    std::mutex control_mutex_;
    
    // 按键状态管理
    std::atomic<bool> button_pressed_{false};
    std::atomic<uint64_t> button_press_time_{0};
    std::atomic<uint64_t> button_release_time_{0};
    
    // 档位配置 - 基于百分比定义
    static constexpr uint8_t SPEED_LEVELS[] = {0, 50, 75, 100};  // 关闭、低、中、高
    static constexpr uint8_t SPEED_LEVEL_COUNT = 4;
    
    // 188数码管显示
    Led188Display* led188_display_;
    
    // 私有方法
    void InitializeHardware();
    void ControlTask();
    void ButtonTask();
    void ProcessCommand(const FanControlRequest& request);
    void UpdatePWM(uint8_t percentage);
    uint8_t GetNextLevel(uint8_t current_percentage);
    uint8_t GetCurrentLevelIndex() const;
    void InitializeMcpTools();
    void ErrorRecovery();

public:
    FanController(gpio_num_t button_gpio, gpio_num_t pwm_gpio, ledc_channel_t pwm_channel);
    ~FanController();
    
    // 统一控制接口 - 基于百分比
    void TurnOff();                                    // 关闭风扇
    void SetPercentage(uint8_t percentage);            // 设置百分比 (0-100)
    void NextLevel();                                  // 下一个档位 (按键专用)
    void EmergencyStop();                              // 紧急停止
    
    // 便捷接口 - 基于档位
    void SetLowSpeed();                                // 设置为低档 (50%)
    void SetMediumSpeed();                             // 设置为中档 (75%)
    void SetHighSpeed();                               // 设置为高档 (100%)
    
    // 轻触按键控制（离线模式）
    void HandleButtonPress();
    void HandleButtonRelease();
    void HandleButtonLongPress();
    
    // 语音控制（在线模式）
    void HandleVoiceCommand(const std::string& command);
    
    // 模式切换
    void SetControlMode(FanControlMode mode);
    FanControlMode GetControlMode() const { return control_mode_.load(); }
    
    // 状态查询 - 统一接口
    bool IsPowerOn() const { return power_.load(); }
    uint8_t GetCurrentPercentage() const { return current_percentage_.load(); }
    uint8_t GetCurrentLevel() const;                   // 获取当前档位索引 (0-3)
    std::string GetCurrentLevelName() const;           // 获取当前档位名称
    
    // 中断服务程序
    void IRAM_ATTR ButtonISR();
    
    // 错误恢复
    void RecoverFromError();
    
    // 188数码管显示相关
    void UpdateLed188Display();
    void SetLed188Display(Led188Display* display);
};

#endif // __FAN_CONTROLLER_H__
