#include "iot/thing.h"
#include "board.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "Battery"
#define BATTERY_UPDATE_INTERVAL_MS 30000  // 30秒更新一次

namespace iot {

// 这里仅定义 Battery 的属性和方法，不包含具体的实现
class Battery : public Thing {
private:
    int level_ = 0;
    bool charging_ = false;
    bool discharging_ = false;
    int64_t last_update_time_ = 0;  // 上次更新时间

    // 在需要时更新电池状态
    void UpdateBatteryStatusIfNeeded() {
        int64_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
        
        // 如果距离上次更新超过指定间隔，则更新状态
        if (current_time - last_update_time_ >= BATTERY_UPDATE_INTERVAL_MS) {
            auto& board = Board::GetInstance();
            board.GetBatteryLevel(level_, charging_, discharging_);
            last_update_time_ = current_time;
            ESP_LOGI(TAG, "1111 Battery level updated: %d%%, Charging: %s", 
                     level_, charging_ ? "Yes" : "No");
        }
    }

public:
    Battery() : Thing("Battery", "The battery of the device") {
        // 定义设备的属性
        properties_.AddNumberProperty("level", "当前电量百分比", [this]() -> int {
            UpdateBatteryStatusIfNeeded();
            return level_;
        });
        properties_.AddBooleanProperty("charging", "是否充电中", [this]() -> int {
            UpdateBatteryStatusIfNeeded();
            return charging_;
        });
    }
};

} // namespace iot

DECLARE_THING(Battery);