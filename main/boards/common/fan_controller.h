#ifndef __FAN_CONTROLLER_H__
#define __FAN_CONTROLLER_H__

#include "mcp_server.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 风扇档位枚举
enum class FanSpeed {
    OFF = 0,
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3
};

class FanController {
private:
    bool power_ = false;
    FanSpeed current_speed_ = FanSpeed::OFF;
    gpio_num_t gpio_num_;
    
    // 通过连续点击实现档位切换
    void SetSpeed(FanSpeed target_speed) {
        // 如果目标档位与当前档位相同，不需要操作
        if (target_speed == current_speed_) {
            return;
        }
        
        // 计算需要点击的次数
        int clicks_needed = 0;
        
        // 从当前档位切换到目标档位需要的点击次数
        switch (current_speed_) {
            case FanSpeed::OFF:
                switch (target_speed) {
                    case FanSpeed::LOW: clicks_needed = 1; break;
                    case FanSpeed::MEDIUM: clicks_needed = 2; break;
                    case FanSpeed::HIGH: clicks_needed = 3; break;
                    default: return; // OFF到OFF不需要操作
                }
                break;
            case FanSpeed::LOW:
                switch (target_speed) {
                    case FanSpeed::OFF: clicks_needed = 3; break; // 低->中->高->关
                    case FanSpeed::MEDIUM: clicks_needed = 1; break;
                    case FanSpeed::HIGH: clicks_needed = 2; break;
                    default: return;
                }
                break;
            case FanSpeed::MEDIUM:
                switch (target_speed) {
                    case FanSpeed::OFF: clicks_needed = 2; break; // 中->高->关
                    case FanSpeed::LOW: clicks_needed = 3; break; // 中->高->关->低
                    case FanSpeed::HIGH: clicks_needed = 1; break;
                    default: return;
                }
                break;
            case FanSpeed::HIGH:
                switch (target_speed) {
                    case FanSpeed::OFF: clicks_needed = 1; break;
                    case FanSpeed::LOW: clicks_needed = 2; break; // 高->关->低
                    case FanSpeed::MEDIUM: clicks_needed = 3; break; // 高->关->低->中
                    default: return;
                }
                break;
        }
        
        // 执行连续点击
        if (clicks_needed > 0) {
            for (int i = 0; i < clicks_needed; i++) {
                // 模拟按键按下
                gpio_set_level(gpio_num_, 1);
                vTaskDelay(pdMS_TO_TICKS(50));  // 按下50ms
                gpio_set_level(gpio_num_, 0);
                vTaskDelay(pdMS_TO_TICKS(100)); // 释放100ms，确保芯片识别
            }
        }
        
        current_speed_ = target_speed;
    }

public:
    FanController(gpio_num_t gpio_num) : gpio_num_(gpio_num) {
        // 配置GPIO引脚
        gpio_config_t config = {
            .pin_bit_mask = (1ULL << gpio_num_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_num_, 0);  // 初始状态为关闭

        // 获取MCP服务器实例并注册工具
        auto& mcp_server = McpServer::GetInstance();
        
        // 注册风扇控制工具
        mcp_server.AddTool("self.fan.get_state", 
                          "Get the current state and speed of the fan", 
                          PropertyList(), 
                          [this](const PropertyList& properties) -> ReturnValue {
                              std::string state = power_ ? "on" : "off";
                              std::string speed_str;
                              switch (current_speed_) {
                                  case FanSpeed::OFF: speed_str = "off"; break;
                                  case FanSpeed::LOW: speed_str = "low"; break;
                                  case FanSpeed::MEDIUM: speed_str = "medium"; break;
                                  case FanSpeed::HIGH: speed_str = "high"; break;
                              }
                              return "{\"power\": " + std::string(power_ ? "true" : "false") + 
                                     ", \"speed\": \"" + speed_str + "\"}";
                          });

        mcp_server.AddTool("self.fan.turn_on", 
                          "Turn on the fan at low speed", 
                          PropertyList(), 
                          [this](const PropertyList& properties) -> ReturnValue {
                              power_ = true;
                              SetSpeed(FanSpeed::LOW);
                              return true;
                          });

        mcp_server.AddTool("self.fan.turn_off", 
                          "Turn off the fan", 
                          PropertyList(), 
                          [this](const PropertyList& properties) -> ReturnValue {
                              power_ = false;
                              SetSpeed(FanSpeed::OFF);
                              return true;
                          });

        mcp_server.AddTool("self.fan.set_speed", 
                          "Set fan speed. speed: 0=off, 1=low, 2=medium, 3=high", 
                          PropertyList({
                              Property("speed", kPropertyTypeInteger, 1, 0, 3)
                          }), 
                          [this](const PropertyList& properties) -> ReturnValue {
                              int speed_value = properties["speed"].value<int>();
                              FanSpeed speed = static_cast<FanSpeed>(speed_value);
                              
                              if (speed == FanSpeed::OFF) {
                                  power_ = false;
                              } else {
                                  power_ = true;
                              }
                              
                              SetSpeed(speed);
                              return true;
                          });

        mcp_server.AddTool("self.fan.next_speed", 
                          "Switch to next speed level (off->low->medium->high->off)", 
                          PropertyList(), 
                          [this](const PropertyList& properties) -> ReturnValue {
                              FanSpeed next_speed = FanSpeed::OFF; // 初始化默认值
                              switch (current_speed_) {
                                  case FanSpeed::OFF: next_speed = FanSpeed::LOW; break;
                                  case FanSpeed::LOW: next_speed = FanSpeed::MEDIUM; break;
                                  case FanSpeed::MEDIUM: next_speed = FanSpeed::HIGH; break;
                                  case FanSpeed::HIGH: next_speed = FanSpeed::OFF; break;
                              }
                              
                              if (next_speed == FanSpeed::OFF) {
                                  power_ = false;
                              } else {
                                  power_ = true;
                              }
                              
                              SetSpeed(next_speed);
                              return true;
                          });
    }
};

#endif // __FAN_CONTROLLER_H__
