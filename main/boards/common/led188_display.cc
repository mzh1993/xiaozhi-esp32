#include "led188_display.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <esp_log.h>

static const char* TAG = "Led188Display";

// 更新命令结构
struct UpdateCommand {
    enum Type {
        SET_VALUE,
        TURN_OFF,
        TURN_ON
    } type;
    
    union {
        uint8_t value;
    } data;
};

Led188Display::Led188Display(gpio_num_t pin1, gpio_num_t pin2, gpio_num_t pin3, gpio_num_t pin4, gpio_num_t pin5) {
    
    // 初始化控制引脚数组
    control_pins_[0] = pin1;
    control_pins_[1] = pin2;
    control_pins_[2] = pin3;
    control_pins_[3] = pin4;
    control_pins_[4] = pin5;
    
    ESP_LOGI(TAG, "Initializing 188 5-wire dynamic matrix display");
    ESP_LOGI(TAG, "Control pins: %d, %d, %d, %d, %d", pin1, pin2, pin3, pin4, pin5);
    ESP_LOGI(TAG, "Dynamic scanning: each pin acts as both anode and cathode");
    
    // 初始化GPIO
    InitializeGPIO();
    
    // 创建更新队列
    update_queue_ = xQueueCreate(10, sizeof(UpdateCommand));
    if (!update_queue_) {
        ESP_LOGE(TAG, "Failed to create update queue");
        return;
    }
    
    // 创建更新任务
    BaseType_t ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<Led188Display*>(param)->UpdateTask();
        },
        "led188_update",
        2048,
        this,
        3,  // 优先级3
        &update_task_,
        0   // 核心0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create update task");
        return;
    }
    
    // 创建动态扫描任务
    ret = xTaskCreatePinnedToCore(
        [](void* param) {
            static_cast<Led188Display*>(param)->ScanTask();
        },
        "led188_scan",
        2048,
        this,
        4,  // 优先级4，高于更新任务
        &scan_task_,
        0   // 核心0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scan task");
        return;
    }
    
    // 初始化显示为关闭状态
    TurnOff();
    
    ESP_LOGI(TAG, "LED188 display initialized successfully");
    ESP_LOGI(TAG, "Display mode: PERCENTAGE only (0-100)");
    
    // 验证段码映射
    ValidateSegmentMapping();
}

Led188Display::~Led188Display() {
    ESP_LOGI(TAG, "Destroying 188 matrix display");
    
    // 停止所有任务
    if (update_task_) {
        vTaskDelete(update_task_);
        update_task_ = nullptr;
    }
    if (scan_task_) {
        vTaskDelete(scan_task_);
        scan_task_ = nullptr;
    }
    
    // 删除队列
    if (update_queue_) {
        vQueueDelete(update_queue_);
        update_queue_ = nullptr;
    }
    
    // 关闭显示
    TurnOff();
    
    ESP_LOGI(TAG, "188 matrix display destroyed");
}

void Led188Display::InitializeGPIO() {
    // 配置控制GPIO引脚
    uint64_t control_mask = 0;
    for (int i = 0; i < 5; i++) {
        control_mask |= (1ULL << control_pins_[i]);
    }
    
    gpio_config_t control_config = {
        .pin_bit_mask = control_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&control_config));
    
    // 初始化引脚状态 (所有引脚低电平 - 关闭所有段)
    SetAllPins(false);
    
    ESP_LOGI(TAG, "GPIO initialized for 188 5-wire dynamic matrix display");
    ESP_LOGW(TAG, "Warning: ESP32S3 GPIO is 3.3V, LED may need 5V. Consider level shifter if needed.");
}

void Led188Display::SetPin(uint8_t pin, bool state) {
    if (pin < 5) {
        gpio_set_level(control_pins_[pin], state ? 1 : 0);
    }
}

void Led188Display::SetAllPins(bool state) {
    for (int i = 0; i < 5; i++) {
        SetPin(i, state);
    }
}

void Led188Display::SetSegment(uint8_t segment_index, bool state) {
    if (segment_index < sizeof(SEGMENT_MAP) / sizeof(SEGMENT_MAP[0])) {
        uint8_t anode = SEGMENT_MAP[segment_index][0];
        uint8_t cathode = SEGMENT_MAP[segment_index][1];
        display_buffer_[anode][cathode].store(state ? 1 : 0);
    }
}

void Led188Display::DisplayDigit(uint8_t digit, uint8_t position) {
    // 数字段码定义 (共阴极) - 标准7段数码管段码
    // 段码顺序: A(顶部), B(右上), C(右下), D(底部), E(左下), F(左上), G(中间)
    static constexpr uint8_t DIGIT_SEGMENTS[10][7] = {
        {1,1,1,1,1,1,0}, // 0: A,B,C,D,E,F
        {0,1,1,0,0,0,0}, // 1: B,C
        {1,1,0,1,1,0,1}, // 2: A,B,D,E,G
        {1,1,1,1,0,0,1}, // 3: A,B,C,D,G
        {0,1,1,0,0,1,1}, // 4: B,C,F,G
        {1,0,1,1,0,1,1}, // 5: A,C,D,F,G
        {1,0,1,1,1,1,1}, // 6: A,C,D,E,F,G
        {1,1,1,0,0,0,0}, // 7: A,B,C
        {1,1,1,1,1,1,1}, // 8: A,B,C,D,E,F,G
        {1,1,1,1,0,1,1}  // 9: A,B,C,D,F,G
    };
    
    if (digit > 9 || position < 1 || position > 3) {
        return;
    }
    
    switch (position) {
        case 1: // DIG1 - 只有B1, C1段，只能显示"1"
            if (digit == 1) {
                SetSegment(SEG_B1, true);
                SetSegment(SEG_C1, true);
            } else {
                SetSegment(SEG_B1, false);
                SetSegment(SEG_C1, false);
            }
            break;
            
        case 2: // DIG2 - 完整7段 (A2-G2)
            for (int i = 0; i < 7; i++) {
                SetSegment(SEG_A2 + i, DIGIT_SEGMENTS[digit][i]);
            }
            break;
            
        case 3: // DIG3 - 完整7段 (A3-G3)
            for (int i = 0; i < 7; i++) {
                SetSegment(SEG_A3 + i, DIGIT_SEGMENTS[digit][i]);
            }
            break;
    }
}

void Led188Display::ValidateSegmentMapping() {
    ESP_LOGI(TAG, "Validating segment mapping...");
    
    // 验证段码映射表的完整性
    const size_t map_size = sizeof(SEGMENT_MAP) / sizeof(SEGMENT_MAP[0]);
    ESP_LOGI(TAG, "Segment map size: %zu", map_size);
    
    // 验证每个段码的阳极和阴极索引都在有效范围内
    for (size_t i = 0; i < map_size; i++) {
        uint8_t anode = SEGMENT_MAP[i][0];
        uint8_t cathode = SEGMENT_MAP[i][1];
        
        if (anode >= 5 || cathode >= 5) {
            ESP_LOGE(TAG, "Invalid segment mapping at index %zu: anode=%d, cathode=%d", i, anode, cathode);
        } else {
            ESP_LOGD(TAG, "Segment %zu: anode=%d, cathode=%d", i, anode, cathode);
        }
    }
    
    // 验证段码索引枚举
    ESP_LOGI(TAG, "Segment indices: B1=%d, C1=%d, A2=%d, B2=%d, C2=%d, D2=%d, E2=%d, F2=%d, G2=%d", 
             SEG_B1, SEG_C1, SEG_A2, SEG_B2, SEG_C2, SEG_D2, SEG_E2, SEG_F2, SEG_G2);
    ESP_LOGI(TAG, "Segment indices: A3=%d, B3=%d, C3=%d, D3=%d, E3=%d, F3=%d, G3=%d, L1=%d, L2=%d", 
             SEG_A3, SEG_B3, SEG_C3, SEG_D3, SEG_E3, SEG_F3, SEG_G3, SEG_L1, SEG_L2);
    
    ESP_LOGI(TAG, "Segment mapping validation completed");
}

void Led188Display::ClearDisplayBuffer() {
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            display_buffer_[i][j].store(0);
        }
    }
}

void Led188Display::UpdateDisplayBuffer() {
    // 清空显示缓冲区
    ClearDisplayBuffer();
    
    if (!enabled_.load()) {
        return;  // 显示被禁用
    }
    
    // 更新百分比显示
    uint8_t percentage = current_value_.load();
    if (percentage <= 100) {
        if (percentage == 0) {
            // 显示 "0"
            DisplayDigit(0, 2);
        } else if (percentage <= 99) {
            // 显示两位数
            uint8_t tens = percentage / 10;
            uint8_t ones = percentage % 10;
            
            // 显示十位数在DIG2，个位数在DIG3
            DisplayDigit(tens, 2);
            DisplayDigit(ones, 3);
        }
        
        // 显示百分比符号
        SetSegment(SEG_L2, true);
    }
}

void Led188Display::UpdateDisplay() {
    if (!enabled_.load()) {
        // 显示被禁用，关闭所有段
        SetAllPins(false);
        return;
    }
    
    // 更新显示缓冲区
    UpdateDisplayBuffer();
}

void Led188Display::ScanTask() {
    ESP_LOGI(TAG, "188 5-wire dynamic matrix scan task started");
    
    while (true) {
        if (!enabled_.load()) {
            // 显示被禁用，关闭所有段
            SetAllPins(false);
            vTaskDelay(pdMS_TO_TICKS(50));  // 增加延迟时间
            continue;
        }
        
        // 动态扫描：轮流激活每个pin作为阳极
        for (int anode_pin = 0; anode_pin < 5; anode_pin++) {
            // 关闭所有引脚
            SetAllPins(false);
            
            // 设置当前pin为阳极（高电平）
            SetPin(anode_pin, true);
            
            // 根据显示缓冲区设置阴极引脚
            for (int cathode_pin = 0; cathode_pin < 5; cathode_pin++) {
                if (cathode_pin != anode_pin) {  // 不能同时作为阳极和阴极
                    if (display_buffer_[anode_pin][cathode_pin].load()) {
                        // 点亮LED段：阳极高电平，阴极低电平
                        SetPin(cathode_pin, false);  // 阴极低电平
                    } else {
                        SetPin(cathode_pin, true);   // 阴极高电平（关闭）
                    }
                }
            }
            
            // 保持一段时间
            vTaskDelay(pdMS_TO_TICKS(2));  // 2ms扫描时间
        }
        
        // 完成一轮扫描后，给其他任务一些时间
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms额外延迟
    }
}

void Led188Display::UpdateTask() {
    UpdateCommand cmd;
    
    ESP_LOGI(TAG, "LED188 update task started");
    
    while (true) {
        if (xQueueReceive(update_queue_, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case UpdateCommand::SET_VALUE:
                    current_value_.store(cmd.data.value);
                    ESP_LOGI(TAG, "Display value set to: %d", cmd.data.value);
                    break;
                    
                case UpdateCommand::TURN_OFF:
                    enabled_.store(false);
                    ESP_LOGI(TAG, "Display turned OFF");
                    break;
                    
                case UpdateCommand::TURN_ON:
                    enabled_.store(true);
                    ESP_LOGI(TAG, "Display turned ON");
                    break;
            }
            
            // 更新显示
            UpdateDisplay();
        }
    }
}

uint8_t Led188Display::NumberToSegment(uint8_t number) {
    if (number <= 9) {
        return SEGMENT_CODES[number];
    }
    return SEGMENT_CODES[10];  // 关闭显示
}



// 公共接口实现
void Led188Display::SetValue(uint8_t value) {
    UpdateCommand cmd = {
        .type = UpdateCommand::SET_VALUE,
        .data = {.value = value}
    };
    
    if (update_queue_) {
        xQueueSend(update_queue_, &cmd, pdMS_TO_TICKS(100));
    }
}



void Led188Display::TurnOff() {
    UpdateCommand cmd = {
        .type = UpdateCommand::TURN_OFF
    };
    
    if (update_queue_) {
        xQueueSend(update_queue_, &cmd, pdMS_TO_TICKS(100));
    }
}

void Led188Display::TurnOn() {
    UpdateCommand cmd = {
        .type = UpdateCommand::TURN_ON
    };
    
    if (update_queue_) {
        xQueueSend(update_queue_, &cmd, pdMS_TO_TICKS(100));
    }
}

// 风扇档位显示专用方法
void Led188Display::DisplayFanPercentage(uint8_t percentage) {
    if (percentage <= 100) {
        SetValue(percentage);
        ESP_LOGI(TAG, "Displaying fan percentage: %d%%", percentage);
    } else {
        ESP_LOGW(TAG, "Invalid fan percentage: %d", percentage);
    }
}


