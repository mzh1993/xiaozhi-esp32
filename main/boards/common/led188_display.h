#ifndef __LED188_DISPLAY_H__
#define __LED188_DISPLAY_H__

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <atomic>

// 188数码管显示模式
enum class Led188DisplayMode {
    PERCENTAGE = 0  // 百分比模式: 0-100
};



class Led188Display {
private:
    // GPIO引脚 - 5根控制线
    gpio_num_t control_pins_[5];
    
    // 当前扫描状态
    uint8_t current_scan_pin_{0};  // 当前作为阳极的pin (0-4)
    
    // 显示状态
    std::atomic<uint8_t> current_value_{0};
    std::atomic<bool> enabled_{true};
    
    // 显示缓冲区 (5x5)
    std::atomic<uint8_t> display_buffer_[5][5];  // [row][col] 矩阵
    
    // 控制任务
    TaskHandle_t update_task_;
    TaskHandle_t scan_task_;      // 动态扫描任务
    QueueHandle_t update_queue_;
    
    // 数码管段码定义 (共阴极)
    static constexpr uint8_t SEGMENT_CODES[] = {
        0x3F, // 0
        0x06, // 1
        0x5B, // 2
        0x4F, // 3
        0x66, // 4
        0x6D, // 5
        0x7D, // 6
        0x07, // 7
        0x7F, // 8
        0x6F, // 9
        0x00  // 关闭
    };
    

    
    // 私有方法
    void InitializeGPIO();
    void UpdateDisplay();
    void UpdateTask();
    void ScanTask();
    void SetPin(uint8_t pin, bool state);
    void SetAllPins(bool state);
    void UpdateDisplayBuffer();
    void ClearDisplayBuffer();
    void SetSegment(uint8_t segment_index, bool state);
    void DisplayDigit(uint8_t digit, uint8_t position);  // position: 1=DIG1, 2=DIG2, 3=DIG3
    void ValidateSegmentMapping();  // 验证段码映射正确性
    uint8_t NumberToSegment(uint8_t number);  // 数字转段码
    
    // 段码映射表 (根据电路图) - 正确的5x5矩阵映射
    // 格式: {阳极索引, 阴极索引} (0-4对应外部控制线P1-P5)
    static constexpr uint8_t SEGMENT_MAP[][2] = {
        // DIG1 段码 (2位半数码管的第一位)
        {2, 3},  // B1 (阳极3, 阴极4) - 对应外部P3->P4
        {1, 3},  // C1 (阳极2, 阴极4) - 对应外部P2->P4
        
        // DIG2 段码 (第二位 - 完整7段)
        {1, 2},  // A2 (阳极2, 阴极3) - 对应外部P2->P3
        {2, 1},  // B2 (阳极3, 阴极2) - 对应外部P3->P2
        {3, 2},  // C2 (阳极4, 阴极3) - 对应外部P4->P3
        {3, 1},  // D2 (阳极4, 阴极2) - 对应外部P4->P2
        {4, 1},  // E2 (阳极5, 阴极2) - 对应外部P5->P2
        {4, 2},  // F2 (阳极5, 阴极3) - 对应外部P5->P3
        {4, 3},  // G2 (阳极5, 阴极4) - 对应外部P5->P4
        
        // DIG3 段码 (第三位 - 完整7段)
        {0, 1},  // A3 (阳极1, 阴极2) - 对应外部P1->P2
        {1, 0},  // B3 (阳极2, 阴极1) - 对应外部P2->P1
        {0, 2},  // C3 (阳极1, 阴极3) - 对应外部P1->P3
        {2, 0},  // D3 (阳极3, 阴极1) - 对应外部P3->P1
        {0, 3},  // E3 (阳极1, 阴极4) - 对应外部P1->P4
        {3, 0},  // F3 (阳极4, 阴极1) - 对应外部P4->P1
        {4, 0},  // G3 (阳极5, 阴极1) - 对应外部P5->P1
        
        // 指示符段码
        {2, 4},  // L1 (阳极3, 阴极5) - 闪电符号，对应外部P3->P5
        {1, 4}   // L2 (阳极2, 阴极5) - 百分比符号，对应外部P2->P5
    };
    
    // 段码索引定义 - 便于使用
    enum SegmentIndex {
        // DIG1
        SEG_B1 = 0,
        SEG_C1 = 1,
        
        // DIG2
        SEG_A2 = 2,
        SEG_B2 = 3,
        SEG_C2 = 4,
        SEG_D2 = 5,
        SEG_E2 = 6,
        SEG_F2 = 7,
        SEG_G2 = 8,
        
        // DIG3
        SEG_A3 = 9,
        SEG_B3 = 10,
        SEG_C3 = 11,
        SEG_D3 = 12,
        SEG_E3 = 13,
        SEG_F3 = 14,
        SEG_G3 = 15,
        
        // 指示符
        SEG_L1 = 16,  // 闪电符号
        SEG_L2 = 17   // 百分比符号
    };

public:
    Led188Display(gpio_num_t pin1, gpio_num_t pin2, gpio_num_t pin3, gpio_num_t pin4, gpio_num_t pin5);
    ~Led188Display();
    
    // 显示控制
    void SetValue(uint8_t value);
    void TurnOff();
    void TurnOn();
    
    // 风扇档位显示专用方法
    void DisplayFanPercentage(uint8_t percentage); // 显示百分比 0-100
    
    // 状态查询
    uint8_t GetCurrentValue() const { return current_value_.load(); }
    bool IsEnabled() const { return enabled_.load(); }
};

#endif // __LED188_DISPLAY_H__
