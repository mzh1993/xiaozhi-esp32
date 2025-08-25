#include "fan_controller.h"
#include <algorithm>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

static const char* TAG = "FanController";

FanController::FanController(gpio_num_t button_gpio, gpio_num_t pwm_gpio, ledc_channel_t pwm_channel)
    : button_gpio_(button_gpio), pwm_gpio_(pwm_gpio), pwm_channel_(pwm_channel), led188_display_(nullptr) {
    
    ESP_LOGI(TAG, "Initializing fan controller: Button GPIO%d, PWM GPIO%d, Channel%d", 
             button_gpio_, pwm_gpio_, pwm_channel_);
    
    // 初始化硬件
    InitializeHardware();
    
    // 创建命令队列
    command_queue_ = xQueueCreate(10, sizeof(FanControlRequest));
    if (!command_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return;
    }
    
    // 创建控制任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<FanController*>(param)->ControlTask();
        },
        "fan_ctrl",
        2048,
        this,
        4,  // 优先级4，高于主循环
        &control_task_,
        0   // 核心0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        return;
    }
    
    // 创建按键检测任务
    ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<FanController*>(param)->ButtonTask();
        },
        "fan_btn",
        2048,
        this,
        5,  // 优先级5，最高优先级
        &button_task_,
        0   // 核心0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return;
    }
    
    // 初始化MCP工具
    InitializeMcpTools();
    
    ESP_LOGI(TAG, "Fan controller initialized successfully");
}

FanController::~FanController() {
    ESP_LOGI(TAG, "Destroying fan controller");
    
    // 停止所有任务
    if (control_task_) {
        vTaskDelete(control_task_);
        control_task_ = nullptr;
    }
    if (button_task_) {
        vTaskDelete(button_task_);
        button_task_ = nullptr;
    }
    
    // 删除队列
    if (command_queue_) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    
    // 停止PWM输出
    ledc_stop(LEDC_LOW_SPEED_MODE, pwm_channel_, 0);
    
    ESP_LOGI(TAG, "Fan controller destroyed");
}

void FanController::InitializeHardware() {
    // 配置轻触按键GPIO（带中断）
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << button_gpio_),
        .mode = GPIO_MODE_INPUT,                  // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,         // 启用上拉电阻
        .pull_down_en = GPIO_PULLDOWN_DISABLE,    // 禁用下拉电阻
        .intr_type = GPIO_INTR_ANYEDGE,           // 双边沿触发中断
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
    
    // 安装GPIO中断服务
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    // 添加按键中断处理
    ESP_ERROR_CHECK(gpio_isr_handler_add(button_gpio_, 
        [](void* arg) {
            static_cast<FanController*>(arg)->ButtonISR();
        }, this));
    
    // 配置LEDC PWM硬件
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,        // 使用低速模式
        .duty_resolution = LEDC_TIMER_13_BIT,     // 13位分辨率 (0-8191)
        .timer_num = LEDC_TIMER_0,                // 使用定时器0
        .freq_hz = 25000,                         // 25kHz PWM频率
        .clk_cfg = LEDC_AUTO_CLK,                 // 自动选择时钟源
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    
    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {
        .gpio_num = pwm_gpio_,                    // PWM输出引脚
        .speed_mode = LEDC_LOW_SPEED_MODE,        // 低速模式
        .channel = pwm_channel_,                  // 使用通道0
        .intr_type = LEDC_INTR_DISABLE,           // 禁用中断
        .timer_sel = LEDC_TIMER_0,                // 使用定时器0
        .duty = 0,                                // 初始占空比为0
        .hpoint = 0,                              // 初始高电平点为0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI(TAG, "Hardware initialized: Button GPIO%d, PWM GPIO%d, Channel%d, 25kHz@13bit", 
             button_gpio_, pwm_gpio_, pwm_channel_);
}

void IRAM_ATTR FanController::ButtonISR() {
    uint32_t gpio_num = gpio_get_level(button_gpio_);
    uint64_t current_time = esp_timer_get_time();
    
    if (gpio_num == 0) {  // 按键按下（低电平）
        button_pressed_.store(true);
        button_press_time_.store(current_time);
    } else {  // 按键释放（高电平）
        button_pressed_.store(false);
        button_release_time_.store(current_time);
    }
}

void FanController::ButtonTask() {
    const TickType_t xDelay = pdMS_TO_TICKS(10);  // 10ms检测间隔
    const uint64_t LONG_PRESS_TIME = 2000000;     // 2秒长按时间
    const uint64_t DEBOUNCE_TIME = 50000;         // 50ms防抖时间
    
    ESP_LOGI(TAG, "Button task started");
    
    while (true) {
        if (button_pressed_.load()) {
            uint64_t press_duration = esp_timer_get_time() - button_press_time_.load();
            
            // 检测长按
            if (press_duration > LONG_PRESS_TIME) {
                HandleButtonLongPress();
                // 等待按键释放
                while (button_pressed_.load()) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        } else {
            // 检测短按
            uint64_t release_time = button_release_time_.load();
            uint64_t press_time = button_press_time_.load();
            
            if (release_time > press_time && 
                (release_time - press_time) < LONG_PRESS_TIME &&
                (release_time - press_time) > DEBOUNCE_TIME) {
                
                HandleButtonPress();
                // 重置时间，避免重复触发
                button_press_time_.store(0);
                button_release_time_.store(0);
            }
        }
        
        vTaskDelay(xDelay);
    }
}

void FanController::ControlTask() {
    FanControlRequest request;
    
    ESP_LOGI(TAG, "Control task started");
    
    while (true) {
        if (xQueueReceive(command_queue_, &request, portMAX_DELAY) == pdTRUE) {
            ProcessCommand(request);
        }
    }
}

void FanController::ProcessCommand(const FanControlRequest& request) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    
    ESP_LOGI(TAG, "Processing command: %d, percentage: %d, from_voice: %s", 
              static_cast<int>(request.command), request.percentage, 
              request.from_voice ? "true" : "false");
    
    switch (request.command) {
        case FanCommand::TURN_OFF:
            TurnOff();
            break;
        case FanCommand::SET_PERCENTAGE:
            SetPercentage(request.percentage);
            break;
        case FanCommand::NEXT_LEVEL:
            NextLevel();
            break;
        case FanCommand::EMERGENCY_STOP:
            EmergencyStop();
            break;
    }
}

// 统一控制接口 - 基于百分比
void FanController::TurnOff() {
    power_.store(false);
    current_percentage_.store(0);
    UpdatePWM(0);
    ESP_LOGI(TAG, "Fan turned OFF");
}

void FanController::SetPercentage(uint8_t percentage) {
    if (percentage > 100) percentage = 100;
    
    power_.store(percentage > 0);
    current_percentage_.store(percentage);
    UpdatePWM(percentage);
    
    // 更新188数码管显示
    UpdateLed188Display();
    
    ESP_LOGI(TAG, "Fan set to %d%%", percentage);
}

void FanController::NextLevel() {
    uint8_t next_percentage = GetNextLevel(current_percentage_.load());
    SetPercentage(next_percentage);
    ESP_LOGI(TAG, "Fan switched to next level: %d%%", next_percentage);
}

void FanController::EmergencyStop() {
    ESP_LOGW(TAG, "EMERGENCY STOP triggered!");
    TurnOff();
}

// 便捷接口 - 基于档位
void FanController::SetLowSpeed() {
    SetPercentage(SPEED_LEVELS[1]);  // 50%
}

void FanController::SetMediumSpeed() {
    SetPercentage(SPEED_LEVELS[2]);  // 75%
}

void FanController::SetHighSpeed() {
    SetPercentage(SPEED_LEVELS[3]);  // 100%
}

// 辅助方法
uint8_t FanController::GetNextLevel(uint8_t current_percentage) {
    // 找到当前百分比最接近的档位
    uint8_t current_level = GetCurrentLevelIndex();
    uint8_t next_level = (current_level + 1) % SPEED_LEVEL_COUNT;
    return SPEED_LEVELS[next_level];
}

uint8_t FanController::GetCurrentLevelIndex() const {
    uint8_t current_percent = current_percentage_.load();
    
    // 找到最接近的档位
    uint8_t closest_level = 0;
    uint8_t min_diff = 255;
    
    for (uint8_t i = 0; i < SPEED_LEVEL_COUNT; i++) {
        uint8_t diff = abs(current_percent - SPEED_LEVELS[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_level = i;
        }
    }
    
    return closest_level;
}

uint8_t FanController::GetCurrentLevel() const {
    return GetCurrentLevelIndex();
}

std::string FanController::GetCurrentLevelName() const {
    uint8_t level = GetCurrentLevelIndex();
    switch (level) {
        case 0: return "off";
        case 1: return "low";
        case 2: return "medium";
        case 3: return "high";
        default: return "unknown";
    }
}

void FanController::HandleButtonPress() {
    if (control_mode_ != FanControlMode::OFFLINE) {
        ESP_LOGW(TAG, "Button press ignored - not in offline mode");
        return;
    }
    
    FanControlRequest request = {
        .command = FanCommand::NEXT_LEVEL,
        .percentage = 0,
        .from_voice = false
    };
    
    if (xQueueSend(command_queue_, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send button press command to queue");
    }
}

void FanController::HandleButtonRelease() {
    // 按键释放事件，目前不需要特殊处理
    ESP_LOGD(TAG, "Button released");
}

void FanController::HandleButtonLongPress() {
    if (control_mode_ != FanControlMode::OFFLINE) {
        ESP_LOGW(TAG, "Button long press ignored - not in offline mode");
        return;
    }
    
    FanControlRequest request = {
        .command = FanCommand::EMERGENCY_STOP,
        .percentage = 0,
        .from_voice = false
    };
    
    if (xQueueSend(command_queue_, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send emergency stop command to queue");
    }
}

void FanController::HandleVoiceCommand(const std::string& command) {
    if (control_mode_ != FanControlMode::ONLINE) {
        ESP_LOGW(TAG, "Voice command ignored - not in online mode");
        return;
    }
    
    FanControlRequest request;
    request.from_voice = true;
    
    // 解析语音命令 - 统一为百分比控制
    if (command.find("关闭") != std::string::npos || command.find("关") != std::string::npos) {
        request.command = FanCommand::TURN_OFF;
        request.percentage = 0;
    } else if (command.find("低风") != std::string::npos || command.find("小风") != std::string::npos) {
        request.command = FanCommand::SET_PERCENTAGE;
        request.percentage = SPEED_LEVELS[1];  // 50%
    } else if (command.find("中风") != std::string::npos || command.find("中风") != std::string::npos) {
        request.command = FanCommand::SET_PERCENTAGE;
        request.percentage = SPEED_LEVELS[2];  // 75%
    } else if (command.find("高风") != std::string::npos || command.find("大风") != std::string::npos) {
        request.command = FanCommand::SET_PERCENTAGE;
        request.percentage = SPEED_LEVELS[3];  // 100%
    } else if (command.find("下一档") != std::string::npos) {
        request.command = FanCommand::NEXT_LEVEL;
        request.percentage = 0;
    } else {
        // 尝试解析百分比
        size_t pos = command.find("%");
        if (pos != std::string::npos) {
            std::string num_str = command.substr(0, pos);
            try {
                int percentage = std::stoi(num_str);
                if (percentage >= 0 && percentage <= 100) {
                    request.command = FanCommand::SET_PERCENTAGE;
                    request.percentage = static_cast<uint8_t>(percentage);
                } else {
                    ESP_LOGW(TAG, "Invalid percentage: %d", percentage);
                    return;
                }
            } catch (...) {
                ESP_LOGW(TAG, "Failed to parse percentage from: %s", command.c_str());
                return;
            }
        } else {
            ESP_LOGW(TAG, "Unknown voice command: %s", command.c_str());
            return;
        }
    }
    
    if (xQueueSend(command_queue_, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send voice command to queue");
    }
}

void FanController::SetControlMode(FanControlMode mode) {
    control_mode_.store(mode);
    ESP_LOGI(TAG, "Fan control mode changed to: %s", 
              mode == FanControlMode::OFFLINE ? "OFFLINE" : "ONLINE");
}

void FanController::UpdatePWM(uint8_t percentage) {
    if (percentage == 0) {
        ledc_stop(LEDC_LOW_SPEED_MODE, pwm_channel_, 0);
        return;
    }
    
    uint32_t duty = (percentage * 8191) / 100;  // 13位分辨率
    ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm_channel_, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_channel_);
}

void FanController::InitializeMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 获取风扇状态
    mcp_server.AddTool("self.fan.get_state", 
                      "Get the current state and speed of the fan", 
                      PropertyList(), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          std::string state = power_.load() ? "on" : "off";
                          std::string level_name = GetCurrentLevelName();
                          uint8_t current_percent = current_percentage_.load();
                          
                          return "{\"power\": " + std::string(power_.load() ? "true" : "false") + 
                                 ", \"level\": \"" + level_name + "\"" +
                                 ", \"percentage\": " + std::to_string(current_percent) +
                                 ", \"mode\": \"" + (control_mode_.load() == FanControlMode::OFFLINE ? "offline" : "online") + "\"}";
                      });

    // 设置控制模式
    mcp_server.AddTool("self.fan.set_control_mode", 
                      "Set fan control mode: 0=offline, 1=online", 
                      PropertyList({
                          Property("mode", kPropertyTypeInteger, 1, 0, 1)
                      }), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          int mode = properties["mode"].value<int>();
                          SetControlMode(static_cast<FanControlMode>(mode));
                          return true;
                      });
    
    // 设置百分比
    mcp_server.AddTool("self.fan.set_percentage", 
                      "Set fan speed percentage (0-100)", 
                      PropertyList({
                          Property("percentage", kPropertyTypeInteger, 50, 0, 100)
                      }), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          int percentage = properties["percentage"].value<int>();
                          SetPercentage(static_cast<uint8_t>(percentage));
                          return true;
                      });
    
    // 设置档位
    mcp_server.AddTool("self.fan.set_level", 
                      "Set fan level: 0=off, 1=low, 2=medium, 3=high", 
                      PropertyList({
                          Property("level", kPropertyTypeInteger, 1, 0, 3)
                      }), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          int level = properties["level"].value<int>();
                          if (level >= 0 && level < SPEED_LEVEL_COUNT) {
                              SetPercentage(SPEED_LEVELS[level]);
                          }
                          return true;
                      });
    
    // 下一个档位
    mcp_server.AddTool("self.fan.next_level", 
                      "Switch to next level (off->low->medium->high->off)", 
                      PropertyList(), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          NextLevel();
                          return true;
                      });
    
    // 紧急停止
    mcp_server.AddTool("self.fan.emergency_stop", 
                      "Emergency stop the fan", 
                      PropertyList(), 
                      [this](const PropertyList& properties) -> ReturnValue {
                          EmergencyStop();
                          return true;
                      });
    
    ESP_LOGI(TAG, "MCP tools initialized");
}

void FanController::ErrorRecovery() {
    ESP_LOGW(TAG, "Fan controller error recovery initiated");
    
    // 1. 停止所有任务
    if (control_task_) {
        vTaskDelete(control_task_);
        control_task_ = nullptr;
    }
    if (button_task_) {
        vTaskDelete(button_task_);
        button_task_ = nullptr;
    }
    
    // 2. 重置硬件状态
    ledc_stop(LEDC_LOW_SPEED_MODE, pwm_channel_, 0);
    gpio_reset_pin(button_gpio_);
    gpio_reset_pin(pwm_gpio_);
    
    // 3. 重置状态
    power_.store(false);
    current_percentage_.store(0);
    
    ESP_LOGI(TAG, "Fan controller error recovery completed");
}

void FanController::RecoverFromError() {
    ErrorRecovery();
    
    // 重新初始化
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待1秒
    InitializeHardware();
    
    // 重新创建任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<FanController*>(param)->ControlTask();
        },
        "fan_ctrl",
        2048,
        this,
        4,
        &control_task_,
        0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to recreate control task after error recovery");
        return;
    }
    
    ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<FanController*>(param)->ButtonTask();
        },
        "fan_btn",
        2048,
        this,
        5,
        &button_task_,
        0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to recreate button task after error recovery");
        return;
    }
    
    ESP_LOGI(TAG, "Fan controller recovered from error successfully");
}

void FanController::UpdateLed188Display() {
    if (!led188_display_) {
        return;
    }
    
    uint8_t current_percent = current_percentage_.load();
    bool is_power_on = power_.load();
    
    // 根据当前百分比显示档位
    if (current_percent == 0) {
        // 关闭状态 - 显示0
        led188_display_->DisplayFanLevel(0);
    } else if (current_percent == SPEED_LEVELS[1]) {
        // 低档 (50%)
        led188_display_->DisplayFanLevel(1);
    } else if (current_percent == SPEED_LEVELS[2]) {
        // 中档 (75%)
        led188_display_->DisplayFanLevel(2);
    } else if (current_percent == SPEED_LEVELS[3]) {
        // 高档 (100%)
        led188_display_->DisplayFanLevel(3);
    } else {
        // 自定义百分比，显示百分比数值
        led188_display_->DisplayFanPercentage(current_percent);
    }
    
    ESP_LOGI(TAG, "Updated LED188 display: percentage=%d%%, level=%d", 
             current_percent, GetCurrentLevel());
}

void FanController::SetLed188Display(Led188Display* display) {
    led188_display_ = display;
    ESP_LOGI(TAG, "LED188 display set: %s", display ? "valid" : "null");
    
    // 立即更新显示
    if (display) {
        UpdateLed188Display();
    }
}
